#pragma once

#include <atomic>
#include <vector>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

class ThreadPool;

class Database {
public:
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

    static nlohmann::json GetIdAndPathFromWiki();
    static void AddImgPath(long id, const std::string& path, const std::vector<uint8_t>& data);
    static void AddConnection(long fromId, long toId);
    static void SetHasParsed(long id, bool hasParsed);
    static void UploadEmbeddings(long id, std::vector<float>& full768);

private:
    nlohmann::json CurlGet(const std::string& apiPath, const nlohmann::json& params);
    nlohmann::json CurlPost(const std::string& apiPath, const nlohmann::json& params);

    std::vector<std::pair<long, long>> contents;

    static inline ThreadPool* threadPool = nullptr;

    static inline std::atomic<long> count = 0;
    std::atomic<long> idx = 0;

    const std::string ip = "10.0.0.148:8080";

    CURL* curl = nullptr;
};
