#include <cstdlib>
#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>
#include <iostream>
#include <cstring>
#include <string>
#include <curl/curl.h>
#include <chrono>
#include <pqxx/pqxx>
#include <boost/url.hpp>
#include <csignal>

#include "Renderer.h"
#include "UrlHelper.h"
#include "Indexer.h"
#include "ThreadPool.h"
#include "Fetcher.h"
#include "Parser.h"
#include "Database.h"

// HNSW (Hierarchical Navigable Small World)
// Vector database
// PostgreSQL? pgvector extension?
    // https://www.postgresql.org/download/

// https://github.com/Cyan4973/xxHash

long idx = 0;
std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();

ThreadPool* fetchPool;
ThreadPool* renderPool;
ThreadPool* extractPool;
ThreadPool* databasePool;
void render(const std::string& url);
void extract(const long httpCode, const std::string& url, const std::string& html);
void addToDB();

void Init() {
    UrlHelper::Init();
    Indexer::Init();
    Renderer::InitChromium();
}

void CleanUp() {
    Indexer::CleanUp();

    printf("\n\nSearched %ld sites\n", idx - 1);
    std::chrono::steady_clock::time_point endTime = std::chrono::steady_clock::now();
    std::chrono::hh_mm_ss hms{std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime)};
    std::cout << "Took: " << hms.hours().count() << "h " << hms.minutes().count() << "m " << hms.seconds().count() << "s\n";
}

void signalHandler(int) {
    CleanUp();
}

void fetch(std::string& url) {
    Fetcher& fetcher = Fetcher::GetFetcher();

    UrlHelper::Normalize(url);
    if (fetcher.CheckRobotsTXT(url)) {
        long httpCode = 0;
        printf("\n\n#%ld/%ld, Searching: %s\n", idx + 1, Database::QueueSize() + 1, url.c_str());

        // render contents
        // add data to queue when finished
        renderPool->Enqueue([url = std::move(url)] {
            render(url);
        });
    } else {
        // skip page
        std::cout << "Skipping: \"" << url << "\", against robots.txt\n";
    }
}

void render(const std::string& url) {
    Renderer& renderer = Renderer::GetRenderer();

    // render should pick up first value from queue (make sure to use locks)
    // then use data to render somehow
    // add that data to another queue
    long httpCode = 0;
    // std::string html = Renderer::GetHTML(document, url, &httpCode);
    // std::cout << "rendered html:\n" << html << "\n\n";
    printf("Rendering: %s\n", url.c_str());
    std::string html = renderer.GetHTML(url, &httpCode);

    extractPool->Enqueue([httpCode, url = std::move(url), html = std::move(html)] {
        extract(httpCode, url, html);
    });
}

void extract(const long httpCode, const std::string& url, const std::string& html) {
    // get data from the render queue and extract, then finally add to like final queue
    Parser& parser = Parser::GetParser();
    parser.ParseLinks(databasePool, httpCode, url, html);

    databasePool->Enqueue([] {
        addToDB();
    });
}

void addToDB() {
    // finally get from final queue and add to db when the queue >= X size

}


// Schema:
// Fetch html
// Render (using multiple chromium pages)
// extract data (links for crawler, words for indexer)
// store data(only store once in a while, not every time you finish searching a page)

int main(int argc, const char* argv[]) {
    std::signal(SIGINT, signalHandler);
    startTime = std::chrono::steady_clock::now();

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

    size_t fetchSize = 2;
    size_t renderSize = 2;
    size_t extractSize = 2;
    size_t databaseSize = 2;
    // each pool needs to be assigned to its own pointer. Fetch pool should have a Fetcher
        // renderPool should have a renderer obj, etc
    fetchPool = new ThreadPool(fetchSize);
    renderPool = new ThreadPool(renderSize);
    extractPool = new ThreadPool(extractSize);
    databasePool = new ThreadPool(databaseSize);

    // std::string url = Database::QueueGet();
    std::vector<std::string> temp = { "https://google.com/", "https://sunsetmapledrafts.com/"};

    for (int i = 0; i < temp.size(); ++i) {
        fetchPool->Enqueue([&temp, i] {
            fetch(temp[i]);
        });
    }

    ThreadPool::Wait();

    CleanUp();
    return 0;
}
