#include "WikiDatabase.h"
#include "login.h"
#include "ThreadPool.h"

#include <iostream>

Database::Database() : cx("host=localhost dbname=wikirace user=" + USER + " password=" + PASSWORD) {
}

Database::~Database() {
    printf("Final Commit\n");
    // tx.commit();
}

void Database::InitPool(int threads) {
    threadPool = new ThreadPool(threads);
}

void Database::Init() {
    std::cout << "Database Connected to " << cx.dbname() << "\n";

    cx.prepare("GetCount", "SELECT COUNT(*) FROM wiki");
    cx.prepare("GetIdPathParsed", "SELECT id, path, hasParsed FROM wiki");

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

    cx.prepare(
        "SetHasParsed",
        "UPDATE wiki "
        "SET hasParsed = $2 "
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
        result = tx.exec(pqxx::prepped("GetIdPathParsed"));

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
        tx.commit(); // AddConnection should handle this commit
    });
}

void Database::AddConnection(long fromId, long toId) {
    threadPool->Enqueue([fromId, toId] {
        Database& database = Database::GetDatabase();

        // database.tx.exec(pqxx::prepped("insertConnection"), pqxx::params(fromId, toId));
        // database.tx.exec(pqxx::prepped("increaseAuthority"), pqxx::params(toId));
        
        database.contents.push_back(std::pair(fromId, toId));

        // simply adding "increaseAuthority" or "AddConnection" was causing deadlock
        // so instead, there is a list that contains all the data, then sorts and removes duplicates
        ++count;
        ++database.idx;
        if (database.idx % 1000 == 0) {
            std::sort(database.contents.begin(), database.contents.end(), [](const auto& a, const auto& b) { return a.second < b.second; }); // sort by second

            std::string incAuth = "UPDATE wiki SET authority = wiki.authority + 1 FROM ( VALUES ";
            for (size_t i = 0; i < database.contents.size(); ++i) {
                if (i != 0) incAuth += ", "; 
                incAuth += "(" + std::to_string(database.contents[i].second) + ")";
            }
            incAuth += ") AS v(id) WHERE wiki.id = v.id";

            // remove duplicates
            std::sort(database.contents.begin(), database.contents.end()); // sort by first
            database.contents.erase(std::unique(database.contents.begin(), database.contents.end()), database.contents.end());

            std::string sql = "INSERT INTO connections (id, connection) VALUES ";
            for (size_t i = 0; i < database.contents.size(); ++i) {
                if (i != 0) sql += ", ";
                sql += "(" + std::to_string(database.contents[i].first) + "," + std::to_string(database.contents[i].second) + ")";
            }
            sql += " ON CONFLICT (id, connection) DO NOTHING";

            if (database.idx % 10000 == 0) // only print when 10000
                printf("\033[35m#%ld | %ld thread queue | %ld -> %ld\n\033[0m", count.load(), threadPool->GetQueueSize(), fromId, toId);

            database.contents.clear();
            pqxx::work tx{database.cx};
            tx.exec(sql);
            tx.exec(incAuth);
            tx.commit();
        }
    });
}

void Database::SetHasParsed(long id, bool hasParsed) {
    threadPool->Enqueue([id, hasParsed] {
        Database& database = Database::GetDatabase();

        pqxx::work tx{database.cx};
        tx.exec(pqxx::prepped("SetHasParsed"), pqxx::params(id, hasParsed));
        tx.commit(); // AddConnection should handle this commit

        // ++idx;
        // if (idx % 100 == 0) {
            // printf("\033[34m#%ld | %ld size | uploaded to db: %ld\033[0m\n", idx.load(), threadPool->GetQueueSize(), id);
            // database.tx.commit();
        // }
    });
}
