#include <cstdlib>
#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <iostream>
#include <cstring>
#include <string>
#include <curl/curl.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>
#include <type_traits>
#include <unordered_set>
#include <queue>
#include <chrono>
#include <pqxx/pqxx>
#include "../login.h"

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

int GetHTML(const char* url, std::string* html, long* httpCode);

static size_t write_data(char *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static lxb_status_t callback(const lxb_char_t *data, size_t len, void *ctx) {
    std::string* str = static_cast<std::string*>(ctx);
    str->append(reinterpret_cast<const char*>(data), len);
    return LXB_STATUS_OK;
}

// returns base url
// e.g. https://www.google.com/robots.txt would return "https://www.google.com" AND path == "/"
std::string ExtractOrigin(const std::string& url, std::string& path) {
    // Find "://"
    std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        throw std::runtime_error("Invalid URL");

    // Find first '/' after the host
    std::size_t pathStart = url.find('/', schemeEnd + 3);

    if (pathStart == std::string::npos) {
        path = "/";
        return url;
    } else {
        path = url.substr(pathStart);
        return url.substr(0, pathStart);
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
    std::string origin = ExtractOrigin(url, path);

    // if origin already inside robotsTXT, then find path that fits to rule. If non, i guess allow
    // if not already inside map, then go to origin + "/robots.txt", parse file, and add to map, then check
    auto it = robotsTXT.find(origin);
    if (it == robotsTXT.end()) { // wasn't found
        // go to (origin) + "/robots.txt"
        std::string output;
        long httpCode;
        GetHTML((origin + "/robots.txt").c_str(), &output, &httpCode);

        // parse file
        it = ParseRobotsTXT(origin, output);
    }

    bool allow;
    return it->second.FindRule(path);
}

// whether or not to add this url based on robots.txt or if already visited
bool ShouldVisit(const std::string& url) {
    bool isAllowed = CheckRobotsTXT(url);
    bool hasntVisited = visited.find(url) == visited.end();
    return isAllowed && hasntVisited;
}

void ExecuteSQL(std::string& url, std::string& title, std::string& description, long contentHash) {
    // start a transaction
    pqxx::work tx{cx};

    tx.exec(pqxx::prepped("insert_page"), pqxx::params(url, title, description, contentHash));

    // Commit the transaction
    std::cout << "Making changes definite\n";
    tx.commit();
    std::cout << "OK\n";
}

// may add url to search further
// wont add if already searched through or isn't allowed to visit
void AddURL(long statusCode, std::string& url, std::string& title, std::string& description) {
    // 2xx: good
    // 3xx: follow redirects
    // 4xx: mark as dead / skip
    // 5xx: retry later
    
    if (ShouldVisit(url)) { // if haven't already seen url
        printf("Visited: %s\n", url.c_str());
        if (statusCode < 300)
            ExecuteSQL(url, title, description, 0);
        visited.insert(url);
        queue.push(url);
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
        printf("No title found");

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

    printf("Found %zu link(s).\n\n",
           lxb_dom_collection_length(collection));

    // Step 3: Initialize the URL parser and parse the base URL
    status = lxb_url_parser_init(&url_parser, NULL);
    if (status != LXB_STATUS_OK)
        printf("status is not OK 2.\n");

    base_url = lxb_url_parse(&url_parser, NULL, baseUrlStr, base_url_len);
    if (base_url == NULL)
        printf("base_url is NULL.\n");

    std::string title = GetTitle(document);
    printf("Title: %s\n", title.c_str());
    std::string description = GetDescription(document);
    printf("Description: %s\n", description.c_str());

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
        AddURL(httpCode, resolved_url, title, description);
    }

    // Cleanup
    lxb_dom_collection_destroy(collection, true);
    lxb_url_parser_destroy(&url_parser, false);
    lxb_url_memory_destroy(base_url);
    lxb_html_document_destroy(document);
}

int GetHTML(const char* url, std::string* html, long* httpCode) {
    html->clear(); // reset

    CURL* curl = curl_easy_init();
    if (!curl)
        return -1;

    CURLcode res;
    *httpCode = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, html);

    res = curl_easy_perform(curl); // perform request
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Transfer failed: %s\n", curl_easy_strerror(res));
        return -1;
    }

    // extract the server's HTTP response code
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, httpCode);
    printf("HTTP Status Code: %ld\n\n", *httpCode);

    curl_easy_cleanup(curl);

    return 0;
}

void ConnectToDB() {
    // connect to db
    // std::string connStr = "host=localhost dbname=SearchEngine user=" + USER + " password=" + PASSWORD;
    // std::cout << "connStr: " << connStr << "\n";
    // return;
    // cx = pqxx::connection(connStr);
    std::cout << "Connected to " << cx.dbname() << "\n";

    cx.prepare(
        "insert_page",
        "INSERT INTO siteData(url, title, description, contentHash, lastVisited) VALUES($1, $2, $3, $4, NOW())"
    );
}

int main(int argc, const char* argv[]) {
    // try to parse further
    // things like /index.html, ?tag="...", etc should be considered the same url

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

    ConnectToDB();

    queue.push(url);

    std::string html;
    long httpCode;
    long index = 0;
    while (!queue.empty()) {
        if (depth >= 0 && ++index > depth) // has to be a positive depth value, will stop once depth is reached
            break;

        printf("\n\nSearching: %s\n", queue.front().c_str());
        const unsigned char* urlChar = reinterpret_cast<const unsigned char*>(queue.front().c_str());
        int status = GetHTML(queue.front().c_str(), &html, &httpCode);
        if (status == 0) // good
            ParseLinks(httpCode, urlChar, queue.front().size(), reinterpret_cast<const unsigned char*>(html.c_str()), html.size());
        queue.pop();
    }

    printf("\n\nSearched %ld sites\n", index - 1);

    return 0;
}
