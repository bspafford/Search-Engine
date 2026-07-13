#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <iostream>
#include <cstring>
#include <string>
#include <curl/curl.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>
#include <unordered_map>
#include <queue>
#include <chrono>

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

std::unordered_map<std::string, URLData> visited;
std::queue<std::string> queue;

static size_t write_data(char *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static lxb_status_t callback(const lxb_char_t *data, size_t len, void *ctx) {
    std::string* str = static_cast<std::string*>(ctx);
    str->append(reinterpret_cast<const char*>(data), len);
    return LXB_STATUS_OK;
}

void VisitURL(long statusCode, std::string& url, std::string& title, std::string& description) {
    // 2xx: good
    // 3xx: follow redirects
    // 4xx: mark as dead / skip
    // 5xx: retry later
    
    if (visited.find(url) == visited.end()) { // if haven't already seen url
        printf("Visited: %s\n", url.c_str());
        URLData urlData = {
            title,
            description,
            0,
            std::chrono::steady_clock::now()
        };
        visited.insert(std::pair(url, urlData));
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

    /* Step 1: Parse the HTML document. */
    document = lxb_html_document_create();
    if (document == NULL)
        printf("Something went wrong 1.\n");

    status = lxb_html_document_parse(document, html, html_len);
    if (status != LXB_STATUS_OK)
        printf("Something went wrong 2.\n");

    /* Step 2: Find all <a> elements. */
    collection = lxb_dom_collection_make(&document->dom_document, 128);
    if (collection == NULL)
        printf("Something went wrong 3.\n");

    status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->body), collection, (const lxb_char_t *) "a", 1);
    if (status != LXB_STATUS_OK)
        printf("status is not OK 1.\n");

    printf("Found %zu link(s).\n\n",
           lxb_dom_collection_length(collection));

    /* Step 3: Initialize the URL parser and parse the base URL. */
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

    /* Step 4: Iterate links, extract href, and resolve each URL. */
    for (size_t i = 0; i < lxb_dom_collection_length(collection); i++) {
        element = lxb_dom_collection_element(collection, i);

        href = lxb_dom_element_get_attribute(element,
                                             (const lxb_char_t *) "href", 4,
                                             &href_len);
        if (href == NULL) {
            printf("[%zu] <a> without href, skipping.\n", i);
            continue;
        }

        // printf("[%zu] href: %.*s\n", i, (int) href_len, (const char *) href);

        /* Resolve the href against the base URL. */

        lxb_url_parser_clean(&url_parser);

        url = lxb_url_parse(&url_parser, base_url, href, href_len);
        if (url == NULL) {
            printf("     Failed to parse URL.\n");
            continue;
        }

        // get urls into string
        std::string resolved_url;
        (void) lxb_url_serialize(url, callback, &resolved_url, false);
        VisitURL(httpCode, resolved_url, title, description);
    }

    /* Cleanup. */
    lxb_dom_collection_destroy(collection, true);
    lxb_url_parser_destroy(&url_parser, false);
    lxb_url_memory_destroy(base_url);
    lxb_html_document_destroy(document);
}

int GetHTML(const unsigned char* url, std::string* html, long* httpCode) {
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
    // std::cout << *html << "\n";

    curl_easy_cleanup(curl);

    return 0;
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

    queue.push(url);

    std::string html;
    long httpCode;
    long index = 0;
    while (!queue.empty()) {
        if (depth >= 0 && ++index > depth) // has to be a positive depth value, will stop once depth is reached
            break;

        printf("\n\nSearching: %s\n", queue.front().c_str());
        const unsigned char* urlChar = reinterpret_cast<const unsigned char*>(queue.front().c_str());
        int status = GetHTML(urlChar, &html, &httpCode);
        if (status == 0) // good
            ParseLinks(httpCode, urlChar, queue.front().size(), reinterpret_cast<const unsigned char*>(html.c_str()), html.size());
        queue.pop();
    }

    printf("\n\nSearched %ld sites\n", index - 1);

    return 0;
}
