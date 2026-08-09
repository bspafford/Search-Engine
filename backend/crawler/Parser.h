#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>
#include <curl/curl.h>
#include <pqxx/pqxx>

class ThreadPool;

class Parser {
public:
    static void InitPool(int threads);
    void Init();
    ~Parser();

    static void Parse(const long httpCode, const std::string& url, const std::string& html);
    void ParseLinks(long httpCode, const std::string& urlStr, const std::string& html);

    static Parser& GetParser() {
        thread_local Parser parser;
        thread_local bool initialized = false;
        if (!initialized) {
            parser.Init();
            initialized = true;
        }
        return parser;
    }

    static void AddURL(std::string& url);

private:
    long ExecuteSQL(long httpCode, const std::string& url, std::string& title, std::string& description, long contentHash, std::string& favicon);
    bool IsOriginURL(const std::string url);
    bool IsValidURL(const std::string url);
    // resolves absolute and relative links to absolute
    // e.g.: https://examle.com/blog/page.html
    // /favicon.ico                     --> https://example.com/favicon.ico
    // favicon.ico                      --> https://example.com/blog/favicon.ico
    // https://example.com/favicon.png  --> same
    // //cdn.example.com/favicon.png    --> https://cdn.example.com/favicon.png
    std::string ResolveUrl(const std::string& origin, const std::string& favicon);

    static bool ShouldVisit(std::string& url);

    std::string GetTitle(lxb_html_document_t* document);
    std::string GetDescription(lxb_html_document_t* document);
    std::string GetFavicon(lxb_html_document_t* document);
    std::string DownloadFavicon(lxb_html_document_t* document, const std::string& origin);
    std::vector<unsigned char> DownloadImage(const std::string& url);
    std::string Hash(const std::string& input);

    int timeoutTime = 10;
    int totalTimeoutTime = 30;
    CURLU* u = nullptr;
    CURL* curl = nullptr;
    CURL* imageCurl = nullptr;
    lxb_html_document_t* document = nullptr;
    lxb_dom_collection_t* collection = nullptr;


    static inline std::unordered_set<std::string> visited;
    static inline std::mutex visitedMutex;
    static bool VisitedContains(const std::string& url);
    static void VisitedInsert(const std::string& url);

    static inline ThreadPool* threadPool = nullptr;
};
