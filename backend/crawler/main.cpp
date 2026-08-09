#include <iostream>
#include <string>
#include <chrono>
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

// Schema:
// Fetch html
// Render (using multiple chromium pages)
// extract data (links for crawler, words for indexer)
// store data(only store once in a while, not every time you finish searching a page)
int main(int argc, const char* argv[]) {
    std::signal(SIGINT, signalHandler);
    startTime = std::chrono::steady_clock::now();

    // https://www.notebooklm.google
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
    size_t parseSize = 2;
    size_t databaseSize = 2;
    // each pool needs to be assigned to its own pointer. Fetch pool should have a Fetcher
        // renderPool should have a renderer obj, etc
    Fetcher::InitPool(fetchSize);
    Renderer::InitPool(renderSize);
    Parser::InitPool(parseSize);
    Database::InitPool(databaseSize);

    // std::string url = Database::QueueGet();
    // std::vector<std::string> temp = { "https://google.com/", "https://sunsetmapledrafts.com/"};
    Parser::AddURL(url);
    Database::InitPopulate();

    // for maxing out the queuing, i think that should be relatively fine. Cause the database will always have an "out" and same with the fetcher. Cause the database goes to an unclamped database, and the fetcher will put the urls inside of a database also. So even when capping the enqueue size, and maybe making the other sleep using something like "not_full.wait(lock, [&] { return queue.size() < max_size})"
    Fetcher::Fetch(url);

    ThreadPool::Wait();

    CleanUp();
    return 0;
}
