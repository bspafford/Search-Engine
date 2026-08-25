#pragma once

#include <pqxx/pqxx>
#include <atomic>

class ThreadPool;

class Database {
public:
    Database();
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

private:
    pqxx::connection cx;

    static inline ThreadPool* threadPool = nullptr;

    static inline std::atomic<long> idx = 0;
};
