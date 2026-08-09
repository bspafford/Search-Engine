#pragma once

#include "ThreadPool.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <curl/curl.h>
#include <mutex>

struct Rule {
    std::string rule;
    bool allow = false;

    bool Matches(const std::string_view& path) {
        // example: */*/temp.html
        // matches: hello/world/temp.html

        // break path into lists, splitting '/' (hello, world, temp.html)
        // if list size doesn't match, then not the same, continue searching
        // if they do match, loop through seeing if each string matches. if '*' then free pass

        std::string_view ruleView = rule;
        int pathCount = std::count(path.begin(), path.end(), '/');
        int ruleCount = std::count(ruleView.begin(), ruleView.end(), '/');
        if (pathCount != ruleCount)
            return false;

        size_t pathPos = 0, rulePos = 0;
        while (pathPos != std::string::npos) {
            size_t startPathPos = pathPos;
            size_t startRulePos = rulePos;
            pathPos = path.find('/', pathPos + 1);
            rulePos = rule.find('/', rulePos + 1);
            std::string_view pathStr = path.substr(startPathPos, pathPos - startPathPos);
            std::string_view ruleStr = ruleView.substr(startRulePos, rulePos - startRulePos);

            if (ruleStr != "/*" && ruleStr != "*" && pathStr != ruleStr)
                return false;
        }

        return true;
    }
};

struct RobotInfo {
    std::vector<Rule> rules;
    std::chrono::steady_clock::time_point fetchedAt;
    std::vector<std::string> sitemaps;

    // returns ture if successful
    bool FindRule(const std::string path) {
        for (Rule& rule : rules) {
            if (rule.Matches(path)) { // rule found
                return rule.allow;
            }
        }

        return true; // rule was not found, returning true by default
    }
};

class Fetcher {
public:
    static void InitPool(int threads);
    void Init();

    std::unordered_map<std::string, RobotInfo>::iterator ParseRobotsTXT(const std::string& origin, std::string_view robotsText);
    bool CheckRobotsTXT(const std::string& url);
    std::string CurlGet(const std::string& url, long* httpCode);

    static void Fetch(std::string url);

    static Fetcher& GetFetcher() {
        thread_local Fetcher fetcher;
        thread_local bool initialized = false;
        if (!initialized) {
            fetcher.Init();
            initialized = true;
        }
        return fetcher;
    }

private:
    static size_t write_data(char *contents, size_t size, size_t nmemb, void *userp);

    std::string_view trim(std::string_view s);

    static inline long idx = 0;
    CURL* curl = nullptr;
    float timeoutTime = 10.f;
    float totalTimeoutTime = 30.f;

    static inline std::mutex robotsMutex;
    static inline std::mutex idxMutex;
    static inline std::unordered_map<std::string, RobotInfo> robotsTXT;
    static inline std::string botName = "*";
    static std::unordered_map<std::string, RobotInfo>::iterator RobotsInsert(const std::string& robotsName, RobotInfo robotInfo);
    static std::unordered_map<std::string, RobotInfo>::iterator RobotsFind(const std::string& robotsName);
    static std::unordered_map<std::string, RobotInfo>::iterator RobotsEnd();
    static long IdxIncrement();

    static inline ThreadPool* threadPool = nullptr;
};
