#include "WikiDatabase.h"
#include "login.h"
#include "ThreadPool.h"

#include <iostream>

Database::Database() : cx("host=localhost dbname=wikirace user=" + USER + " password=" + PASSWORD) {

}

void Database::InitPool(int threads) {
    threadPool = new ThreadPool(threads);
}

void Database::Init() {
    std::cout << "Database Connected to " << cx.dbname() << "\n";

    cx.prepare("GetCount", "SELECT COUNT(*) FROM wiki");
    cx.prepare("GetIdAndPath", "SELECT id, path FROM wiki");

    cx.prepare(
        "increaseAuthority",
        "UPDATE wiki "
        "SET authority = authority + 1 "
        "WHERE id = $1"
    );

    cx.prepare(
        "insertConnection",
        "INSERT INTO connections(id, connection) "
        "VALUES($1, $2)"
        "ON CONFLICT (id, connection)"
        "DO NOTHING"
    );

    cx.prepare(
        "AddImgHash",
        "UPDATE wiki "
        "SET thumbnailHash = $2 "
        "WHERE id = $1"
    );
}


size_t Database::GetWikiCount() {
    long count = 0;
    bool done = false;
    std::condition_variable cv;
    std::mutex m;
    threadPool->Enqueue([&] {
        Database& database = Database::GetDatabase();

        pqxx::work tx{database.cx};
        count = tx.query_value<long>(pqxx::prepped("GetCount"));

        // Commit the transaction
        tx.commit();

        {
            std::lock_guard lock(m);
            done = true;
        }

        cv.notify_one();
    });

    std::unique_lock lock(m);
    cv.wait(lock, [&] { return done; });

    return count;
}

pqxx::result Database::GetIdAndPathFromWiki() {
    pqxx::result result;
    bool done = false;
    std::condition_variable cv;
    std::mutex m;
    threadPool->Enqueue([&] {
        Database& database = Database::GetDatabase();

        pqxx::work tx{database.cx};
        result = tx.exec(pqxx::prepped("GetIdAndPath"));

        // Commit the transaction
        tx.commit();

        {
            std::lock_guard lock(m);
            done = true;
        }

        cv.notify_one();
    });

    std::unique_lock lock(m);
    cv.wait(lock, [&] { return done; });

    return result;
}

void Database::AddImgHash(long id, const std::string& hash) {
    threadPool->Enqueue([id, hash = std::move(hash)] {
        Database& database = Database::GetDatabase();

        pqxx::work tx{database.cx};
        tx.exec(pqxx::prepped("AddImgHash"), pqxx::params(id, hash));
        tx.commit();
    });
}

void Database::AddConnection(long fromId, long toId) {
    threadPool->Enqueue([fromId, toId] {
        Database& database = Database::GetDatabase();

        pqxx::work tx{database.cx};
        tx.exec(pqxx::prepped("insertConnection"), pqxx::params(fromId, toId));
        tx.exec(pqxx::prepped("increaseAuthority"), pqxx::params(toId));
        tx.commit();
        ++idx;
        if (idx % 100 == 0)
            printf("#%ld, added to DB: %ld -> %ld\n", idx.load(), fromId, toId);
    });
}
