#include "Database.h"
#include "ThreadPool.h"
#include "Helper.h"

#include <iostream>

size_t write_data(char* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

void Database::InitPool(int threads) {
    threadPool = new ThreadPool(threads);
}

void Database::Init() {
    curl = curl_easy_init();

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
}

void Database::AddWikiSite(const std::string& path, const std::string& title) {
    threadPool->Enqueue([path, title] {
        Database& database = Database::GetDatabase();

        database.CurlPost("/AddWikiSite", { { "path", path }, { "title", title } });
    });
}

nlohmann::json Database::GetIdAndPathFromWiki() {
    nlohmann::json result = nlohmann::json::array();
    bool done = false;
    std::condition_variable cv;
    std::mutex m;
    threadPool->Enqueue([&] {
        Database& database = Database::GetDatabase();

        long pageSize = 100000;
        long offset = 0;
        while (true) { // go until result.size() == 0
            nlohmann::json data = database.CurlGet("/GetIdPathParsed", { { "limit", pageSize }, { "offset", offset } });
            if (data.size())
                std::cout << "path: " << data[0]["path"].get<std::string>() << "\n";
            result.insert(result.end(), data.begin(), data.end());
            printf("\033[32m%s Offset, data size: %s\033[0m\n", Helper::PrettyPrint(offset).c_str(), Helper::PrettyPrint(data.size()).c_str());
            if (data.size() == 0) {
                printf("Finished Loading DB: %s\n", Helper::PrettyPrint(result.size()).c_str());
                break;
            }

            offset += data.size();
        }

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

void Database::AddImgPath(long id, const std::string& path, const std::vector<uint8_t>& data) {
    threadPool->Enqueue([id, path = std::move(path), data = std::move(data)] {
        Database& database = Database::GetDatabase();

        //database.CurlPost("/AddImgPath?id=" + std::to_string(id) + "&thumbnail=" + path, { {"data", data} });

        std::string contents;
        CURLcode res;

        char* encodedValue = curl_easy_escape(database.curl, path.c_str(), path.length());

        std::string apiPath = database.ip + "/AddImgPath?id=" + std::to_string(id) + "&path=" + encodedValue;
        
        curl_easy_setopt(database.curl, CURLOPT_POST, 1L);
        curl_easy_setopt(database.curl, CURLOPT_URL, apiPath.c_str());
        curl_easy_setopt(database.curl, CURLOPT_WRITEDATA, &contents);
        curl_easy_setopt(database.curl, CURLOPT_POSTFIELDS, data.data());
        curl_easy_setopt(database.curl, CURLOPT_POSTFIELDSIZE, data.size());

        res = curl_easy_perform(database.curl);

        if (res != CURLE_OK) {
            printf("Transfer failed: %s | path: %s\n", curl_easy_strerror(res), apiPath.c_str());
            throw std::runtime_error("Transfer failed");
            return;
        }
    });
}

void Database::AddConnection(long fromId, long toId) {
    threadPool->Enqueue([fromId, toId] {
        Database& database = Database::GetDatabase();

        database.contents.push_back({ { "fromId", fromId }, { "toId", toId } });
    });
}

void Database::SetHasParsed(long id, bool hasParsed) {
    threadPool->Enqueue([id, hasParsed] {
        Database& database = Database::GetDatabase();

        ++database.idx;

        //tx.exec(pqxx::prepped("SetHasParsed"), pqxx::params(id, hasParsed));
        nlohmann::json json;
        database.CurlPost("/SetHasParsed", { { "id", id }, { "hasParsed", hasParsed } });

        // Every time has Parsed updates, add conncetions along with it
        // also increases authority
        database.CurlPost("/AddConnections", database.contents);
        if (database.idx % 100 == 0) {
            printf("\033[34mAdded to Database: %ld -> %ld | %ld thread size\033[0m\n", database.contents[0]["fromId"].get<long>(), database.contents[0]["toId"].get<long>(), threadPool->GetQueueSize());
        }
        database.contents.clear();
    });
}

void Database::UploadEmbeddings(long id, std::vector<float>& full768) {
    threadPool->Enqueue([id, full768 = std::move(full768)] {
        Database& database = Database::GetDatabase();

        nlohmann::json json;
        database.CurlPost("/UploadEmbeddings", { { "id", id }, { "768", full768 } });
    });
}

nlohmann::json Database::CurlGet(const std::string& apiPath, const nlohmann::json& params) {
    std::string contents;
    CURLcode res;

    std::string paramKey = "data";
    std::string paramValue = params.dump();

    char* encodedKey = curl_easy_escape(curl, paramKey.c_str(), paramKey.length());
    char* encodedValue = curl_easy_escape(curl, paramValue.c_str(), paramValue.length());

    std::string path = ip + apiPath + "?" + encodedKey + "=" + encodedValue;
    curl_easy_setopt(curl, CURLOPT_URL, path.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &contents);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        printf("Transfer failed: %s | path: %s\n", curl_easy_strerror(res), path.c_str());
        throw std::runtime_error("Transfer failed");
        return {};
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    //std::cout << "httpCode: " << httpCode << "\n";

    if (contents.empty())
        contents = "{}";
    return nlohmann::json::parse(contents);
}

nlohmann::json Database::CurlPost(const std::string& apiPath, const nlohmann::json& params) {
    std::string contents;
    CURLcode res;

    std::string path = ip + apiPath;
    std::string data = params.dump();

    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, path.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &contents);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.size());

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        printf("Transfer failed: %s | path: %s\n", curl_easy_strerror(res), path.c_str());
        throw std::runtime_error("Transfer failed");
        return {};
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    //std::cout << "httpCode: " << httpCode << "\n";

    if (contents.empty())
        contents = "{}";
    return nlohmann::json::parse(contents);
}

long Database::GetThreadPoolSize() {
    return threadPool->GetQueueSize();
}