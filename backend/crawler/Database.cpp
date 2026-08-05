#include "Database.h"
#include "login.h"

#include <iostream>

Database::Database() : cx("host=localhost dbname=SearchEngine user=" + USER + " password=" + PASSWORD) {

}

void Database::Init() {
    std::cout << "Connected to " << cx.dbname() << "\n";

    cx.prepare(
        "insert_page",
        "INSERT INTO siteData(url, title, description, contentHash, lastVisited, favicon) "
        "VALUES($1, $2, $3, $4, NOW(), $5) "
        "ON CONFLICT (url) "
        "DO UPDATE SET "
        "title = EXCLUDED.title, "
        "description = EXCLUDED.description, "
        "contentHash = EXCLUDED.contentHash, "
        "lastVisited = NOW(), "
        "favicon = EXCLUDED.favicon "
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
        "INSERT INTO inverted_index (wordId, urlId, count) VALUES ($1, $2, $3) "
        "ON CONFLICT (wordId, urlId) DO NOTHING"
    );
}

// gets top {maxQueueSize} from queue DB and inserts into queue
void Database::PopulateSiteQueue() {
    std::cout << "PopulateSiteQueue\n";
    pqxx::work tx{cx};
    // std::string sql = "SELECT * FROM siteData LIMIT " + std::to_string(maxQueueSize);
    std::string sql = "DELETE FROM visitQueue WHERE url IN (SELECT url FROM visitQueue LIMIT 100) "
                      "RETURNING url;";

    pqxx::result result = tx.exec(sql);

    for (pqxx::row_ref row : result) {
        QueueAdd(row["url"].as<std::string>());
    }

    // if DB was empty, populate from urls list instead
    if (QueueSize() == 0) {
        for (const std::string& url : UrlsCopy())
            QueueAdd(url);
        UrlsClear();
    }

    tx.commit();
}

long Database::InsertPage(const std::string& url, const std::string& title, const std::string& description, long contentHash, const std::string& favicon) {
    // start a transaction
    pqxx::work tx{cx};

    bool addTrailingSlash = url.back() != '/'; // add trailing slash if it doesn't have one already
    std::cout << "adding to DB!: " << url << (addTrailingSlash ? "/" : "") << "\n";
    long urlId = tx.query_value<long>(pqxx::prepped("insert_page"), pqxx::params((url + std::string(addTrailingSlash ? "/" : "")), title, description, contentHash, favicon));

    // Commit the transaction
    tx.commit();

    return urlId;
}

void Database::IndexerAddToDB(long urlId, const std::string& url, std::unordered_map<std::string, int> counts) { // start a transaction
    pqxx::work tx{cx};

    for (auto& [word, count] : counts) {
        int wordId = tx.query_value<int>(pqxx::prepped("insertWord"), pqxx::params(word));
        // std::cout << "adding \"" << word << "\" (" << wordId << "), url: \"" << url << " (" << urlId << "), count: " << count << "\n";
        tx.exec(pqxx::prepped("insertInvertedIndex"), pqxx::params(wordId, urlId, count));
    }

    // Commit the transaction
    tx.commit();
}

void Database::QueueAdd(const std::string& v) {
    std::unique_lock<std::mutex> lock(queueMutex);
    queue.push(v);
}

size_t Database::QueueSize() {
    std::unique_lock<std::mutex> lock(queueMutex);
    return queue.size();
}

std::string Database::QueueGet() {
    std::unique_lock<std::mutex> lock(queueMutex);
    std::string url = queue.front();
    queue.pop();

    // if queue empty, populate it
    if (queue.empty())
        PopulateSiteQueue();

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
