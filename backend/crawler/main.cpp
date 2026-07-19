#include <cstdlib>
#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <string>
#include <curl/curl.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>
#include <type_traits>
#include <unordered_set>
#include <queue>
#include <chrono>
#include <pqxx/pqxx>
#include <boost/url.hpp>
#include "../login.h"

#include "renderJS.h"
#include "UrlHelper.h"

// HNSW (Hierarchical Navigable Small World)
// Vector database
// PostgreSQL? pgvector extension?
    // https://www.postgresql.org/download/

// https://github.com/Cyan4973/xxHash

// should make the visited map persist, in case of crashes

struct URLData {
    std::string title;
    std::string description;
    uint64_t hash;
    std::chrono::steady_clock::time_point lastVisited;
};

struct Rule {
    std::string path;
    bool allow = false;

    bool Matches(const std::string& path) {
        return this->path == path; // temp, simple string matching
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

std::unordered_set<std::string> visited;
std::unordered_map<std::string, RobotInfo> robotsTXT;
std::queue<std::string> queue;
pqxx::connection cx("host=localhost dbname=SearchEngine user=" + USER + " password=" + PASSWORD);
std::string botName = "*";
CURLU* u = nullptr;

static lxb_status_t callback(const lxb_char_t *data, size_t len, void *ctx) {
    std::string* str = static_cast<std::string*>(ctx);
    str->append(reinterpret_cast<const char*>(data), len);
    return LXB_STATUS_OK;
}

// https://google.com: true
// https://google.com/aboutUs false
bool IsOriginURL(const std::string url) {
    std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        throw std::runtime_error("Invalid URL (IsOriginURL): " + url);

    // Find first '/' after the host
    std::size_t pathStart = url.find('/', schemeEnd + 3);

    return pathStart == std::string::npos || pathStart == url.size() - 1;
}

// returns base url
// e.g. https://www.google.com/robots.txt would return "https://www.google.com" AND path == "/"
std::string ExtractOrigin(const std::string& url, std::string* path) {
    // Find "://"
    std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        throw std::runtime_error("Invalid URL (ExtractOrigin): " + url);

    // Find first '/' after the host
    std::size_t pathStart = url.find('/', schemeEnd + 3);

    // make sure to have trailing '/' on URLs
    if (pathStart == std::string::npos) {
        if (path) *path = "/";
        bool addTrailing = url.back() != '/';
        return url + std::string(addTrailing ? "/" : "");
    } else {
        if (path) *path = url.substr(pathStart);
        return url.substr(0, pathStart) + "/";
    }
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(s.front()))
        s.remove_prefix(1);

    while (!s.empty() && std::isspace(s.back()))
        s.remove_suffix(1);

    size_t pos = s.find('#');
    if (pos != std::string_view::npos)
        s = s.substr(0, pos);

    return s;
}

std::unordered_map<std::string, RobotInfo>::iterator ParseRobotsTXT(const std::string& origin, std::string_view robotsText) {
    RobotInfo robotInfo;
    robotInfo.fetchedAt = std::chrono::steady_clock::now();
    auto [it, success] = robotsTXT.insert({ origin, robotInfo });

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
bool CheckRobotsTXT(const std::string& url) {
    std::string path;
    std::string origin = ExtractOrigin(url, &path);

    // if origin already inside robotsTXT, then find path that fits to rule. If non, i guess allow
    // if not already inside map, then go to origin + "/robots.txt", parse file, and add to map, then check
    auto it = robotsTXT.find(origin);
    if (it == robotsTXT.end()) { // wasn't found
        // go to (origin) + "/robots.txt"
        std::cout << "getting robots.txt for: " << origin << "\n";
        long httpCode;
        std::string output = Renderer::CurlGet(std::string(origin + "robots.txt"), &httpCode);

        if (httpCode >= 300 || httpCode == 0) {
            robotsTXT.insert({ origin, {} }); // add blank input, didn't find robots.txt
            std::cout << "problem finding \"" << origin << "robots.txt\", http code: " << httpCode << ", returning\n";
            return true;
        }

        std::cout << "got it, now parsing\n";

        // parse file
        it = ParseRobotsTXT(origin, output);

        std::cout << "finished parsing\n";
    }

    bool allow;
    return it->second.FindRule(path);
}

// whether or not to add this url based on robots.txt or if already visited
bool ShouldVisit(std::string& url) {
    UrlHelper::Normalize(url); // fix formatting
    bool hasntVisited = visited.find(url) == visited.end();
    return hasntVisited;
}

void ExecuteSQL(long httpCode, std::string& url, std::string& title, std::string& description, long contentHash, std::string& favicon) {
    // 2xx: good
    // 3xx: follow redirects
    // 4xx: mark as dead / skip
    // 5xx: retry later
    if (httpCode >= 300) {
        std::cout << "bad http code: " << httpCode << " on: " << url << ", returning\n";
        return;
    }

    // if url IS the base, then do it
    if (!IsOriginURL(url))
        return;

    // start a transaction
    pqxx::work tx{cx};

    bool addTrailingSlash = url.back() != '/'; // add trailing slash if it doesn't have one already
    std::cout << "adding to DB!: " << url << (addTrailingSlash ? "/" : "") << "\n";
    tx.exec(pqxx::prepped("insert_page"), pqxx::params((url + std::string(addTrailingSlash ? "/" : "")), title, description, contentHash, favicon));

    // Commit the transaction
    tx.commit();
}

// may add url to search further
// wont add if already searched through or isn't allowed to visit
void AddURL(std::string& url) {
    if (ShouldVisit(url)) { // if haven't already seen url
        visited.insert(url);
        queue.push(url);

        // if haven't visited origin url then add to queue
        std::string origin = ExtractOrigin(url, nullptr);
        if (visited.find(origin) == visited.end())
            AddURL(origin);
    }
}

std::string GetTitle(lxb_html_document_t* document) {
    // get title
    lxb_dom_collection_t* title = lxb_dom_collection_make(&document->dom_document, 1);
    if (!title) {
        printf("title is null");
    }

    lxb_status_t status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->head), title, (const lxb_char_t*)"title", 5);
    if (status != LXB_STATUS_OK) {
        printf("No title found");
    }

    size_t titleLen;
    lxb_dom_element_t* titleElement = lxb_dom_collection_element(title, 0);
    if (!titleElement)
        return "";

    lxb_char_t* titleChars = lxb_dom_node_text_content(lxb_dom_interface_node(titleElement), &titleLen);
    std::string titleString(reinterpret_cast<const char*>(titleChars));

    lxb_dom_collection_destroy(title, true);
    return titleString;
}

std::string GetDescription(lxb_html_document_t* document) {
    lxb_dom_element_t* element;
    const lxb_char_t* name;
    size_t len;

    lxb_dom_collection_t* description = lxb_dom_collection_make(&document->dom_document, 32);
    if (!description)
        printf("description is null");

    lxb_status_t status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->head), description, (const lxb_char_t*)"meta", 4);
    if (status != LXB_STATUS_OK)
        printf("No description found");

    for (int i = 0; i < lxb_dom_collection_length(description); ++i) {
        element = lxb_dom_collection_element(description, i);
        name = lxb_dom_element_get_attribute(element, (const lxb_char_t*) "name", 4, &len);
        if (!name) // was no name attribute
            continue;

        if (std::strcmp(reinterpret_cast<const char*>(name), "description") != 0)
            continue;

        name = lxb_dom_element_get_attribute(element, (const lxb_char_t*) "content", 7, &len);
        std::string descriptionStr(reinterpret_cast<const char*>(name));

        lxb_dom_collection_destroy(description, true);
        return descriptionStr;
    }

    /* Cleanup. */
    lxb_dom_collection_destroy(description, true);
    return "No description found";
}

std::string GetFavicon(lxb_html_document_t* document) {
    lxb_dom_element_t* element;
    const lxb_char_t* rel;
    size_t len;

    lxb_dom_collection_t* favicon = lxb_dom_collection_make(&document->dom_document, 32);
    if (!favicon)
        printf("favicon is null");

    lxb_status_t status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->head), favicon, (const lxb_char_t*)"link", 4);
    if (status != LXB_STATUS_OK)
        printf("No favicon found");

    std::cout << "favicon length: " << lxb_dom_collection_length(favicon) << "\n";
    for (int i = 0; i < lxb_dom_collection_length(favicon); ++i) {
        element = lxb_dom_collection_element(favicon, i);
        rel = lxb_dom_element_get_attribute(element, (const lxb_char_t*) "rel", 3, &len);

        if (!rel) // was no name attribute
            continue;

        if (std::strcmp(reinterpret_cast<const char*>(rel), "icon") != 0)
            continue;

        rel = lxb_dom_element_get_attribute(element, (const lxb_char_t*) "href", 4, &len);
        std::string faviconStr(reinterpret_cast<const char*>(rel));

        lxb_dom_collection_destroy(favicon, true);
        std::cout << "favicon: " << faviconStr << "\n";
        return faviconStr;
    }

    /* Cleanup. */
    lxb_dom_collection_destroy(favicon, true);
    std::cout << "No favicon found 1\n";
    return "";
}

// resolves absolute and relative links to absolute
// e.g.: https://examle.com/blog/page.html
// /favicon.ico                     --> https://example.com/favicon.ico
// favicon.ico                      --> https://example.com/blog/favicon.ico
// https://example.com/favicon.png  --> same
// //cdn.example.com/favicon.png    --> https://cdn.example.com/favicon.png
std::string ResolveUrl(const std::string& origin, const std::string& favicon) {
    boost::urls::url_view base(origin);
    boost::urls::url_view relative(favicon);
    boost::urls::url absolute;

    boost::urls::resolve(base, relative, absolute);

    return absolute.buffer();
}

std::string DownloadFavicon(lxb_html_document_t* document, const std::string& origin) {
    std::string favicon = GetFavicon(document);

    std::string resolvedUrl = "";
    if (favicon.empty()) // test if default "/favicon.ico" exists first
        resolvedUrl = origin + (origin.back() != '/' ? "/" : "") + "favicon.ico";
    else
        resolvedUrl = ResolveUrl(origin, favicon);

    // download
    std::cout << "resolvedUrl: " << resolvedUrl << "\n";

    std::vector<unsigned char> data = Renderer::DownloadImage(resolvedUrl);

    if (data.empty()) // was no favicon
        return "";

    // save data to file
    std::string fileName = Renderer::Hash(resolvedUrl);
    std::ofstream file("/var/www/html/favicons/" + fileName, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return fileName;
}

bool IsValidURL(const std::string url) {
    CURLUcode rc = curl_url_set(u, CURLUPART_URL, url.c_str(), 0);
    bool isValid = rc == CURLUE_OK;
    return isValid;
}

void ParseLinks(long httpCode, const unsigned char* baseUrlStr, size_t urlLen, const unsigned char* html, size_t htmlLen) {
    lxb_status_t status;
    lxb_html_document_t *document;
    lxb_dom_collection_t *collection;
    lxb_dom_element_t *element;
    lxb_url_parser_t url_parser;
    lxb_url_t *base_url, *url;
    const lxb_char_t *href;
    size_t href_len;

    size_t html_len = htmlLen;
    size_t base_url_len = urlLen;

    // Step 1: Parse the HTML document
    document = lxb_html_document_create();
    if (document == NULL)
        printf("Something went wrong 1.\n");

    status = lxb_html_document_parse(document, html, html_len);
    if (status != LXB_STATUS_OK)
        printf("Something went wrong 2.\n");

    // Step 2: Find all <a> elements
    collection = lxb_dom_collection_make(&document->dom_document, 128);
    if (collection == NULL)
        printf("Something went wrong 3.\n");

    status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->body), collection, (const lxb_char_t *) "a", 1);
    if (status != LXB_STATUS_OK)
        printf("status is not OK 1.\n");

    printf("Found %zu link(s).\n\n", lxb_dom_collection_length(collection));


    // Step 3: Initialize the URL parser and parse the base URL
    status = lxb_url_parser_init(&url_parser, NULL);
    if (status != LXB_STATUS_OK)
        printf("status is not OK 2.\n");

    base_url = lxb_url_parse(&url_parser, NULL, baseUrlStr, base_url_len);
    if (base_url == NULL)
        printf("base_url is NULL.\n");

    std::string urlStr(reinterpret_cast<const char*>(baseUrlStr));
    std::cout << "baseURlStr: " << urlStr << ", isorigin? " << IsOriginURL(urlStr) << "\n";

    std::string title = GetTitle(document);
    std::string description = GetDescription(document);
    std::string favicon = DownloadFavicon(document, urlStr);

    if (IsOriginURL(urlStr)) // only add to database if Origin URL
        ExecuteSQL(httpCode, urlStr, title, description, 0, favicon);

    // Step 4: Iterate links, extract href, and resolve each URL
    for (size_t i = 0; i < lxb_dom_collection_length(collection); i++) {
        element = lxb_dom_collection_element(collection, i);

        href = lxb_dom_element_get_attribute(element,
                                             (const lxb_char_t *) "href", 4,
                                             &href_len);
        if (href == NULL) {
            printf("[%zu] <a> without href, skipping.\n", i);
            continue;
        }

        // Resolve the href against the base URL
        lxb_url_parser_clean(&url_parser);
        url = lxb_url_parse(&url_parser, base_url, href, href_len);
        if (url == NULL) {
            printf("     Failed to parse URL.\n");
            continue;
        }

        // get urls into string
        std::string resolved_url;
        (void) lxb_url_serialize(url, callback, &resolved_url, false);
        if (IsValidURL(resolved_url)) {
            UrlHelper::Normalize(resolved_url);
            AddURL(resolved_url);
        }
    }

    // Cleanup
    lxb_dom_collection_destroy(collection, true);
    lxb_url_parser_destroy(&url_parser, false);
    lxb_url_memory_destroy(base_url);
    lxb_html_document_destroy(document);
}

void ConnectToDB() {
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
        "favicon = EXCLUDED.favicon"
    );
}

void Init() {
    u = curl_url();
    Renderer::Init();
    UrlHelper::Init();
    ConnectToDB();
}

void CleanUp() {
    curl_url_cleanup(u);
    Renderer::CleanUp();
}

int main(int argc, const char* argv[]) {
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();

    std::string url = "https://www.google.com"; // default URL
    long depth = 10; // default depth

    if (argc == 1)
        printf("No url entered, going with default: %s\n", url.c_str());
    else
        url = argv[1];

    if (argc <= 2)
        printf("No depth entered, going with default: %ld\n", depth);
    else
        depth = std::stol(argv[2]);

    std::cout << "going with url: " << url << "\n";
    std::cout << "going with depth: " << depth << "\n";

    Init();

    UrlHelper::Normalize(url);
    return 0;

    AddURL(url);

    std::string html;
    long httpCode;
    long index = 0;
    while (!queue.empty()) {
        if (depth >= 0 && index > depth) // has to be a positive depth value, will stop once depth is reached
            break;

        url = queue.front();
        queue.pop();
        UrlHelper::Normalize(url);
        if (CheckRobotsTXT(url)) {
            printf("\n\n#%ld/%ld, Searching: %s\n", index + 1, queue.size() + 1, url.c_str());
            const unsigned char* urlChar = reinterpret_cast<const unsigned char*>(url.c_str());
            std::string html = Renderer::GetHTML(url, &httpCode);
            ParseLinks(httpCode, urlChar, url.size(), reinterpret_cast<const unsigned char*>(html.c_str()), html.size());
        } else {
            std::cout << "Skipping: \"" << url << "\", against robots.txt\n";
        }

        ++index;
    }

    printf("\n\nSearched %ld sites\n", index - 1);
    std::chrono::steady_clock::time_point endTime = std::chrono::steady_clock::now();
    std::chrono::hh_mm_ss hms{std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime)};
    std::cout << "Took: " << hms.hours().count() << "h " << hms.minutes().count() << "m " << hms.seconds().count() << "s\n";
    
    CleanUp();

    return 0;
}
