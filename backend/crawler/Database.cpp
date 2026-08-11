#include "Database.h"
#include "login.h"
#include "ThreadPool.h"

#include <iostream>
#include <stdexcept>

Database::Database() : cx("host=localhost dbname=SearchEngine user=" + USER + " password=" + PASSWORD) {

}

void Database::InitPool(int threads) {
    threadPool = new ThreadPool(threads);
}

void Database::Init() {
    std::cout << "Connected to " << cx.dbname() << "\n";

    cx.prepare(
        "insert_page",
        "INSERT INTO siteData(url, title, description, contentHash, lastVisited, favicon, documentLength) "
        "VALUES($1, $2, $3, $4, NOW(), $5, $6) "
        "ON CONFLICT (url) "
        "DO UPDATE SET "
        "title = EXCLUDED.title, "
        "description = EXCLUDED.description, "
        "contentHash = EXCLUDED.contentHash, "
        "lastVisited = NOW(), "
        "favicon = EXCLUDED.favicon, "
        "documentLength = EXCLUDED.documentLength "
        "RETURNING id"
    );

    cx.prepare(
        "insertWord",
        "INSERT INTO words (word) VALUES ($1) "
        "ON CONFLICT (word) "
        "DO UPDATE SET word = EXCLUDED.word "
        "RETURNING id"
    );

    cx.prepare(
        "insertInvertedIndex",
        "INSERT INTO inverted_index (wordId, urlId, count, field) VALUES ($1, $2, $3, $4) "
        "ON CONFLICT (wordId, urlId, field) DO NOTHING"
    );

    cx.prepare(
        "insertWordPosition",
        "INSERT INTO wordPositions (siteId, wordId, position) VALUES($1, $2, $3) "
        "ON CONFLICT (siteId, wordId, position) DO NOTHING"
    );

    cx.prepare(
        "increaseAuthority",
        "UPDATE siteData "
        "SET authority = authority + 1 "
        "WHERE url = $1"
    );
}

// gets top {maxQueueSize} from queue DB and inserts into queue
void Database::PopulateSiteQueue() {
    std::cout << "PopulateSiteQueue\n";

    // wait for getdatbase to return a value before continuing on thread
    std::string url;
    bool done = false;
    std::condition_variable cv;
    std::mutex m;
    threadPool->Enqueue([&] {
        Database& database = Database::GetDatabase();
        std::cout << "getting urls from database\n";

        pqxx::work tx{database.cx};
        // std::string sql = "SELECT * FROM siteData LIMIT " + std::to_string(maxQueueSize);
        std::string sql = "DELETE FROM visitQueue WHERE url IN (SELECT url FROM visitQueue LIMIT 100) "
                          "RETURNING url;";

        pqxx::result result = tx.exec(sql);

        std::cout << "got results, printing:\n";
        for (pqxx::row_ref row : result) {
            std::cout << "results: " << row["url"].as<std::string>() << "\n";
            queue.push(row["url"].as<std::string>());
        }

        // if DB was empty, populate from urls list instead
        std::cout << "size: " << queue.size() << "\n";
        if (queue.size() == 0) { // if empty, move url list to queue
            printf("queue empty, moving urls into queue: %d\n", (int)UrlsCopy().size());
            for (const std::string& url : UrlsCopy())
                queue.push(url);
            UrlsClear();
        }
        std::cout << "queue size before: " << queue.size() << "\n";
        std::cout << "commit\n";

        tx.commit();

        {
            std::cout << "before m\n";
            std::lock_guard lock(m);
            std::cout << "after m\n";
            done = true;
        }
        std::cout << "notify\n";

        cv.notify_one();
    });

    std::cout << "locking\n";
    std::unique_lock lock(m);
    cv.wait(lock, [&] { return done; });

    std::cout << "queue size after: " << queue.size() << "\n";
}

long Database::InsertPage(const std::string& url, const std::string& title, const std::string& description, long contentHash, const std::string& favicon, const long documentLength) {
    long urlId = 0;
    bool done = false;
    std::condition_variable cv;
    std::mutex m;
    threadPool->Enqueue([&] {
        Database& database = Database::GetDatabase();

        pqxx::work tx{database.cx};
        bool addTrailingSlash = url.back() != '/'; // add trailing slash if it doesn't have one already
        std::cout << "adding to DB!: " << url << (addTrailingSlash ? "/" : "") << "\n";
        urlId = tx.query_value<long>(pqxx::prepped("insert_page"), pqxx::params((url + std::string(addTrailingSlash ? "/" : "")), title, description, contentHash, favicon, documentLength));

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

    printf("\033[35mInserted Page: %s (%ld)\033[0m\n", url.c_str(), urlId);
    return urlId;
}

void Database::IndexerAddToDB(long urlId, const std::string& url, const std::vector<WordData>& words) { // start a transaction
    threadPool->Enqueue([urlId, url, words = std::move(words)] {
        Database& database = Database::GetDatabase();
        pqxx::work tx{database.cx};
        std::unordered_map<WordData, int, WordDataHash> counts;

        // temporarily cache wordIds for insertWordPosition to access
        std::unordered_map<std::string, long> wordIds;

        // words list to counts
        for (const WordData& data : words)
            ++counts[data];

        // prevents Postgres from Deadlocking
        std::vector<std::pair<WordData, int>> wordCounts(counts.begin(), counts.end());
        std::sort(wordCounts.begin(), wordCounts.end(),
            [](const auto& a, const auto& b) {
                if (a.first.word != b.first.word)
                    return a.first.word < b.first.word;

                if (a.first.field != b.first.field)
                    return a.first.field < b.first.field;

                return a.second < b.second;
            });
        // insert words count
        for (auto& [wordData, count] : wordCounts) {
            int wordId = tx.query_value<int>(pqxx::prepped("insertWord"), pqxx::params(wordData.word));
            // std::cout << "adding \"" << word << "\" (" << wordId << "), url: \"" << url << " (" << urlId << "), count: " << count << "\n";
            tx.exec(pqxx::prepped("insertInvertedIndex"), pqxx::params(wordId, urlId, count, wordData.field));

            wordIds[wordData.word] = wordId;
        }

        // insert word positions
        for (int i = 0; i < words.size(); ++i)
            tx.exec(pqxx::prepped("insertWordPosition"), pqxx::params(urlId, wordIds[words[i].word], i + 1));

        // Commit the transaction
        tx.commit();
    });
}

void Database::IncreaseAuthority(std::string url) {
    threadPool->Enqueue([url = std::move(url)] {
        Database& database = Database::GetDatabase();
        pqxx::work tx{database.cx};

        tx.exec(pqxx::prepped("increaseAuthority"), pqxx::params(url));

        // Commit the transaction
        tx.commit();
    });
}

void Database::QueueAdd(const std::string& v) {
    std::unique_lock<std::mutex> lock(queueMutex);
    queue.push(v);
}

size_t Database::QueueSize() {
    std::unique_lock<std::mutex> lock(queueMutex);
    return queue.size();
}

void Database::InitPopulate() {
    long urlId = 0;
    bool done = false;
    std::condition_variable cv;
    std::mutex m;
    threadPool->Enqueue([&] {
        Database& database = Database::GetDatabase();
        std::unique_lock<std::mutex> lock(queueMutex);
        PopulateSiteQueue();
        {
            std::lock_guard lock(m);
            done = true;
        }

        cv.notify_one();
    });

    std::unique_lock lock(m);
    cv.wait(lock, [&] { return done; });
}

std::string Database::QueueGet() {
    std::unique_lock<std::mutex> lock(queueMutex);
    std::string url;

    url = queue.front();
    queue.pop();

    // if queue empty, populate it
    if (queue.empty()) {
        std::cout << "trying to get data from database\n";
        PopulateSiteQueue();
    }

    if (url.empty())
        throw std::runtime_error("url is empty!\n");

    return std::move(url);
}

void Database::UrlsClear() {
    std::unique_lock<std::mutex> lock(urlsMutex);
    urls.clear();
}

std::vector<std::string> Database::UrlsCopy() {
    std::unique_lock<std::mutex> lock(urlsMutex);
    return urls;
}

size_t Database::UrlsSize() {
    std::unique_lock<std::mutex> lock(urlsMutex);
    return urls.size();
}

std::string Database::UrlsGet(size_t i) {
    std::unique_lock<std::mutex> lock(urlsMutex);
    return urls[i];
}

void Database::UrlsAdd(const std::string& url) {
    std::unique_lock<std::mutex> lock(urlsMutex);

    urls.push_back(url);

    printf("urls size: %d\n", (int)urls.size());
    if (urls.size() < maxQueueSize)
        return; // smaller than max size, return

    // if urls is greater than max size
    // upload values to db
    // remove them from the queue
    std::string sql = "INSERT INTO visitQueue (url) SELECT x.url FROM (VALUES ";

    pqxx::work tx{cx};
    for (int i = 0; i < urls.size(); ++i) {
        if (i != 0)
            sql += ", ";

        sql += "(" + tx.quote(urls[i]) + ")";
    }

    sql += ") AS x(url) "
           "WHERE NOT EXISTS ("
           "    SELECT 1 FROM siteData s WHERE s.url = x.url"
           ")"
           "ON CONFLICT (url) DO NOTHING;";

    tx.exec(sql);
    tx.commit();

    // clear urls
    urls.clear();
}
