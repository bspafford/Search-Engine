#pragma once

#include <pqxx/pqxx>
#include <atomic>
#include <vector>
#include <unordered_set>

class ThreadPool;

class Database {
public:
    Database();
    ~Database();
    static void InitPool(int threads);
    void Init();

    static Database& GetDatabase() {
        thread_local Database database;
        thread_local bool initialized = false;
        if (!initialized) {
            database.Init();
            initialized = true;
        }
        return database;
    }

    static size_t GetWikiCount();
    static pqxx::result GetIdAndPathFromWiki();
    static void AddImgHash(long id, const std::string& hash);
    static void AddConnection(long fromId, long toId);
    static void SetHasParsed(long id, bool hasParsed);

private:
    pqxx::connection cx;

    std::vector<std::pair<long, long>> contents;

    static inline ThreadPool* threadPool = nullptr;

    static inline std::atomic<long> count = 0;
    std::atomic<long> idx = 0;
};
