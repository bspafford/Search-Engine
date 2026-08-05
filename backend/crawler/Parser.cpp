#include "Parser.h"
#include "Indexer.h"
#include "UrlHelper.h"
#include "Database.h"
#include "ThreadPool.h"

#include <iostream>
#include <string>
#include <pqxx/pqxx>
#include <boost/url.hpp>
#include <fstream>
#include <openssl/sha.h>

static size_t write_data(char *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

size_t write_byte_data1(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* buffer = static_cast<std::vector<unsigned char>*>(userp);

    unsigned char* bytes = static_cast<unsigned char*>(contents);
    buffer->insert(buffer->end(), bytes, bytes + total);

    return total;
}

static lxb_status_t callback(const lxb_char_t *data, size_t len, void *ctx) {
    std::string* str = static_cast<std::string*>(ctx);
    str->append(reinterpret_cast<const char*>(data), len);
    return LXB_STATUS_OK;
}

void Parser::Init() {
    u = curl_url();

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeoutTime);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, totalTimeoutTime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);

    imageCurl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeoutTime);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, totalTimeoutTime);
    curl_easy_setopt(imageCurl, CURLOPT_WRITEFUNCTION, write_byte_data1);
    curl_easy_setopt(imageCurl, CURLOPT_FOLLOWLOCATION, 1L);

    document = lxb_html_document_create();
    if (document == NULL)
        printf("Document is NULL\n");

    collection = lxb_dom_collection_make(&document->dom_document, 128);
    if (collection == NULL)
        throw std::runtime_error("Collection is NULL\n");
}

Parser::~Parser() {
    curl_url_cleanup(u);
    curl_easy_cleanup(curl);
    curl_easy_cleanup(imageCurl);

    if (collection) {
        lxb_dom_collection_destroy(collection, true);
        collection = nullptr;
    }

    if (document) {
        lxb_html_document_destroy(document);
        document = nullptr;
    }
}

void Parser::ParseLinks(ThreadPool* databasePool, long httpCode, const std::string& urlStr, const std::string& html) {
    lxb_status_t status;
    lxb_dom_element_t *element;
    lxb_url_parser_t url_parser;
    lxb_url_t *base_url, *url;
    const lxb_char_t *href;
    size_t href_len;

    // Parse the HTML document
    status = lxb_html_document_parse(document, reinterpret_cast<const lxb_char_t*>(html.c_str()), html.size());
    if (status != LXB_STATUS_OK)
        printf("Something went wrong 2.\n");

    status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->body), collection, (const lxb_char_t *) "a", 1);
    if (status != LXB_STATUS_OK)
        printf("status is not OK 1.\n");

    printf("Found %zu link(s).\n\n", lxb_dom_collection_length(collection));

    // Initialize the URL parser and parse the base URL
    status = lxb_url_parser_init(&url_parser, NULL);
    if (status != LXB_STATUS_OK)
        printf("status is not OK 2.\n");

    base_url = lxb_url_parse(&url_parser, NULL, reinterpret_cast<const unsigned char*>(urlStr.c_str()), urlStr.size());
    if (base_url == NULL)
        printf("base_url is NULL.\n");

    std::cout << "baseURlStr: " << urlStr << ", isorigin? " << IsOriginURL(urlStr) << "\n";

    std::string title = GetTitle(document);
    std::string description = GetDescription(document);
    std::string favicon = DownloadFavicon(document, urlStr);

    if (IsOriginURL(urlStr)) { // only add to database if Origin URL
        long urlId = ExecuteSQL(databasePool, httpCode, urlStr, title, description, 0, favicon);
        Indexer::ExtractKeywords(databasePool, urlId, urlStr, document, collection);
    }

    // Iterate links, extract href, and resolve each URL
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
            AddURL(databasePool, resolved_url);
        }
    }

    // Cleanup
    // lxb_dom_collection_clean(collection);
    lxb_dom_collection_destroy(collection, true);
    collection = lxb_dom_collection_make(&document->dom_document, 128);

    lxb_url_parser_destroy(&url_parser, false);
    lxb_url_memory_destroy(base_url);

    // lxb_html_document_clean(document);
    lxb_html_document_destroy(document);
    document = lxb_html_document_create();
}

// whether or not to add this url based on robots.txt or if already visited
bool Parser::ShouldVisit(std::string& url) {
    UrlHelper::Normalize(url); // fix formatting
    bool hasntVisited = !VisitedContains(url);
    return hasntVisited;
}

// may add url to search further
// wont add if already searched through or isn't allowed to visit
void Parser::AddURL(ThreadPool* databasePool, std::string& url) {
    if (ShouldVisit(url)) { // if haven't already seen url
        VisitedInsert(url);

        databasePool->Enqueue([url] {
            Database& database = Database::GetDatabase();
            database.UrlsAdd(url);
        });

        // if haven't visited origin url then add to queue
        std::string origin = UrlHelper::ExtractOrigin(url, nullptr);
        if (!VisitedContains(origin))
            AddURL(databasePool, origin);
    }
}

std::string Parser::DownloadFavicon(lxb_html_document_t* document, const std::string& origin) {
    std::string favicon = GetFavicon(document);

    std::string resolvedUrl = "";
    if (favicon.empty()) // test if default "/favicon.ico" exists first
        resolvedUrl = origin + (origin.back() != '/' ? "/" : "") + "favicon.ico";
    else
        resolvedUrl = ResolveUrl(origin, favicon);

    // download
    std::cout << "resolvedUrl: " << resolvedUrl << "\n";

    std::vector<unsigned char> data = DownloadImage(resolvedUrl);

    if (data.empty()) // was no favicon
        return "";

    // save data to file
    std::string fileName = Hash(resolvedUrl);
    std::ofstream file("/var/www/html/favicons/" + fileName, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return fileName;
}

std::vector<unsigned char> Parser::DownloadImage(const std::string& url) {
    std::vector<unsigned char> data;

    curl_easy_setopt(imageCurl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(imageCurl, CURLOPT_WRITEDATA, &data);


    CURLcode result = curl_easy_perform(imageCurl);

    if (result != CURLE_OK)
        data.clear();

    return data;
}

std::string Parser::GetTitle(lxb_html_document_t* document) {
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
    if (!titleElement) {
        lxb_dom_collection_destroy(title, true);
        return "";
    }

    lxb_char_t* titleChars = lxb_dom_node_text_content(lxb_dom_interface_node(titleElement), &titleLen);
    std::string titleString(reinterpret_cast<const char*>(titleChars));

    lxb_dom_collection_destroy(title, true);
    return titleString;
}

std::string Parser::GetDescription(lxb_html_document_t* document) {
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

std::string Parser::GetFavicon(lxb_html_document_t* document) {
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

// https://google.com: true
// https://google.com/aboutUs false
bool Parser::IsOriginURL(const std::string url) {
    std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        throw std::runtime_error("Invalid URL (IsOriginURL): " + url);

    // Find first '/' after the host
    std::size_t pathStart = url.find('/', schemeEnd + 3);

    return pathStart == std::string::npos || pathStart == url.size() - 1;
}

long Parser::ExecuteSQL(ThreadPool* databasePool, long httpCode, const std::string& url, std::string& title, std::string& description, long contentHash, std::string& favicon) {
    // 2xx: good
    // 3xx: follow redirects
    // 4xx: mark as dead / skip
    // 5xx: retry later
    if (httpCode >= 300) {
        std::cout << "bad http code: " << httpCode << " on: " << url << ", returning\n";
        return -1;
    }

    // if url IS the base, then do it
    if (!IsOriginURL(url))
        return -1;

    // Send to thread pool, so I dont have to redefine database connection
    long urlId = 0;
    bool done = false;
    std::condition_variable cv;
    std::mutex m;
    databasePool->Enqueue([&] {
        Database& database = Database::GetDatabase();
        urlId = database.InsertPage(url, title, description, contentHash, favicon);
        {
            std::lock_guard lock(m);
            done = true;
        }

        cv.notify_one();
    });

    std::unique_lock lock(m);
    cv.wait(lock, [&] { return done; });

    return urlId;
}

// resolves absolute and relative links to absolute
// e.g.: https://examle.com/blog/page.html
// /favicon.ico                     --> https://example.com/favicon.ico
// favicon.ico                      --> https://example.com/blog/favicon.ico
// https://example.com/favicon.png  --> same
// //cdn.example.com/favicon.png    --> https://cdn.example.com/favicon.png
std::string Parser::ResolveUrl(const std::string& origin, const std::string& favicon) {
    boost::urls::url_view base(origin);
    boost::urls::url_view relative(favicon);
    boost::urls::url absolute;

    boost::urls::resolve(base, relative, absolute);

    return absolute.buffer();
}

bool Parser::IsValidURL(const std::string url) {
    CURLUcode rc = curl_url_set(u, CURLUPART_URL, url.c_str(), 0);
    bool isValid = rc == CURLUE_OK;
    return isValid;
}

std::string Parser::Hash(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];

    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash);

    std::stringstream ss;
    for (unsigned char c : hash) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }

    return ss.str();
}

bool Parser::VisitedContains(const std::string& url) {
    std::unique_lock<std::mutex> lock(visitedMutex);
    return visited.contains(url);
}

void Parser::VisitedInsert(const std::string& url) {
    std::unique_lock<std::mutex> lock(visitedMutex);
    visited.insert(url);
}
