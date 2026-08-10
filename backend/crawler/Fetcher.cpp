#include "Fetcher.h"
#include "UrlHelper.h"
#include "Database.h"
#include "Renderer.h"

#include <iostream>

void Fetcher::InitPool(int threads) {
    threadPool = new ThreadPool(threads);
}

void Fetcher::Init() {
    std::cout << "\033[33mInit Fetcher\n\033[0m";
    curl = curl_easy_init();

    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeoutTime);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, totalTimeoutTime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
}

void Fetcher::Fetch(std::string url) {
    threadPool->Enqueue([url = std::move(url)]() mutable {
        std::cout << GetIdx() << " > " << maxDepth << "\n";
        if (GetIdx() > maxDepth) return;

        Fetcher& fetcher = Fetcher::GetFetcher();

        // printf("fetching: %s\n", url.c_str());
        UrlHelper::Normalize(url);
        if (fetcher.CheckRobotsTXT(url)) {
            long httpCode = 0;
            printf("\033[34m#%ld/%ld, Searching: %s\n\033[0m", IdxIncrement(), Database::QueueSize() + 1, url.c_str());

            // render contents
            // add data to queue when finished
            Renderer::Render(url);
        } else {
            // skip page
            std::cout << "Skipping: \"" << url << "\", against robots.txt\n";
        }
    });
}

std::unordered_map<std::string, RobotInfo>::iterator Fetcher::ParseRobotsTXT(const std::string& origin, std::string_view robotsText) {
    RobotInfo robotInfo;
    robotInfo.fetchedAt = std::chrono::steady_clock::now();
    auto it = RobotsInsert(origin, robotInfo);


    size_t start = 0;
    bool active = false;
    bool inUserAgentBlock = false;
    while (start < robotsText.size()) {
        size_t end = robotsText.find('\n', start);

        if (end == std::string_view::npos)
            end = robotsText.size();

        std::string_view line = trim(robotsText.substr(start, end - start));

        if (!line.empty()) {
            // parse line
            auto colon = line.find(':');
            std::string_view key = trim(line.substr(0, colon));
            std::string_view value = trim(line.substr(colon + 1));

            if (key == "User-agent") {
                // can have something like:
                //      User-agent: *
                //      User-agent: Yandex
                // so right after its declaring another bot. meaning the program should realize that it needs to see: set bot, (dis)allow, set bot, for it to actually change bots
                if (!inUserAgentBlock) {
                    active = false;
                    inUserAgentBlock = true;
                }

                if (value == botName)
                    active = true;

            } else if (key == "Sitemap") {
                it->second.sitemaps.push_back(std::string(value));
                inUserAgentBlock = false;
            } else if (active) {
                bool allow = key == "Allow";
                it->second.rules.push_back({ std::string(value), allow });
                inUserAgentBlock = false;
            }
        }

        start = end + 1;
    }

    return it;
}

// Check to see if url is allowed to be crawled
// Will also find, parse, and add to RobotsTXT map if not already in there
bool Fetcher::CheckRobotsTXT(const std::string& url) {
    std::string path;
    std::string origin = UrlHelper::ExtractOrigin(url, &path);

    // if origin already inside robotsTXT, then find path that fits to rule. If non, i guess allow
    // if not already inside map, then go to origin + "/robots.txt", parse file, and add to map, then check
    auto it = RobotsFind(origin);
    if (it == RobotsEnd()) { // wasn't found
        // go to (origin) + "/robots.txt"
        // std::cout << "getting robots.txt for: " << origin << "\n";
        long httpCode;
        std::string output = CurlGet(std::string(origin + "robots.txt"), &httpCode);

        if (httpCode >= 300 || httpCode == 0) {
            RobotsInsert(origin, {}); // add blank input, didn't find robots.txt
            // std::cout << "problem finding \"" << origin << "robots.txt\", http code: " << httpCode << ", returning\n";
            return true;
        }

        // std::cout << "got it, now parsing\n";

        // parse file
        it = ParseRobotsTXT(origin, output);

        // std::cout << "finished parsing\n";
    }

    bool allow;
    return it->second.FindRule(path);
}

std::string_view Fetcher::trim(std::string_view s) {
    while (!s.empty() && std::isspace(s.front()))
        s.remove_prefix(1);

    while (!s.empty() && std::isspace(s.back()))
        s.remove_suffix(1);

    size_t pos = s.find('#');
    if (pos != std::string_view::npos)
        s = s.substr(0, pos);

    return s;
}

std::string Fetcher::CurlGet(const std::string& url, long* httpCode) {
    std::string html = "";
    CURLcode res;

    // std::cout << "curl: " << curl << "\n";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);

    res = curl_easy_perform(curl); // perform request

    if (res != CURLE_OK) {
        printf("Transfer failed: %s | url: %s, code: %ld\n", curl_easy_strerror(res), url.c_str(), httpCode ? *httpCode : -1);

        // throw std::runtime_error("Failed!, res is not ok\n");
        if (httpCode) *httpCode = -1;
        return "";
    }

    // extract the server's HTTP response code
    if (httpCode) {
        *httpCode = 0; // init to 0
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, httpCode);
        // printf("HTTP Status Code: %ld, for: %s\n\n", *httpCode, url.c_str());
    }

    return html;
}

size_t Fetcher::write_data(char *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}


std::unordered_map<std::string, RobotInfo>::iterator Fetcher::RobotsInsert(const std::string& robotsName, RobotInfo robotInfo) {
    std::unique_lock<std::mutex> lock(robotsMutex);
    auto [it, _] = robotsTXT.insert({ robotsName, robotInfo });
    return it;
}

std::unordered_map<std::string, RobotInfo>::iterator Fetcher::RobotsFind(const std::string& robotsName) {
    std::unique_lock<std::mutex> lock(robotsMutex);
    return robotsTXT.find(robotsName);
}

std::unordered_map<std::string, RobotInfo>::iterator Fetcher::RobotsEnd() {
    return robotsTXT.end();
}

long Fetcher::IdxIncrement() {
    std::unique_lock<std::mutex> lock(idxMutex);
    return ++idx;
}

long Fetcher::GetIdx() {
    std::unique_lock<std::mutex> lock(idxMutex);
    return idx;
}
