#include <iostream>
#include <filesystem>
#include <string>
#include <zim/archive.h>
#include <windows.h>

#include "Parser.h"
#include "Database.h"
#include "Model.h"
#include "ThreadPool.h"
#include "Helper.h"

void BuildWikiDB(const zim::Archive& archive);
void BuildConnections(const zim::Archive archive);
bool IsRedirect(const zim::Entry& entry);


int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::string zimPath = "C:/Users/bspaf/AppData/Roaming/kiwix-desktop/wikipedia_en_all_maxi_2026-02.zim";
    if (!std::filesystem::exists(zimPath) || std::filesystem::is_directory(zimPath)) {
        std::cout << "Invalid path: \"" << zimPath << "\"\n";
        return 1;
    }
    zim::Archive archive(zimPath);

    //zim::Archive archive(zimPath);
    //zim::Entry entry = archive.getEntryByPath("_assets_/c8f24dc75f9c782269c846c9b17e400f/Symbol_category_class.svg.png");
    //std::cout << "entry: " << entry.getTitle() << ", path: " << entry.getPath() << "\n";
    //return 0;

    bool buildConnections = false;
    if (!buildConnections) { // make wiki database
        Database::InitPool(10);
        BuildWikiDB(archive);
    } else { // build connections
        Model::Init();
        Database::InitPool(10);
        //Parser::InitPool(10);
        Parser::InitPool(1);

        BuildConnections(archive);

        ThreadPool::Wait();
    }
}

void BuildWikiDB(const zim::Archive& archive) {
    long idx = 0;
    long redirectCount = 0;

    long articleCount = archive.getArticleCount();
    std::cout << "Articles: #" << articleCount << "\n";

    lxb_html_document_t* document = lxb_html_document_create();
    if (document == NULL)
        printf("Document is NULL\n");

    lxb_dom_collection_t* collection = lxb_dom_collection_make(&document->dom_document, 128);
    if (collection == NULL)
        throw std::runtime_error("Collection is NULL\n");

    for (auto& entry : archive.iterByTitle()) {
        std::string path = entry.getPath();
        if (Parser::IsRedirect(archive, document, collection, path, nullptr)) {
            ++redirectCount;
            if (redirectCount % 1000 == 0)
                std::cout << "\033[33m#" << Helper::PrettyPrint(redirectCount) << ": \"" << entry.getTitle() << "\" is a redirect\033[0m\n";
            continue;
        }

        ++idx;
        Database::AddWikiSite(path, entry.getTitle());

        if (idx % 1000 == 0)
            std::cout << "\033[34m#" << Helper::PrettyPrint(idx) << " / " << Helper::PrettyPrint(articleCount) << " | " << Helper::PrettyPrint(Database::GetThreadPoolSize()) << " database size | \"" << entry.getTitle() << "\", path: \"" << path << "\"\033[0m\n";
    }
}

void BuildConnections(const zim::Archive archive) {
    long articleCount = archive.getArticleCount();
    std::cout << "Articles: " << articleCount << "\n";

    long idx = 0;
    long redirectCount = 0;
    for(auto& entry : archive.iterByTitle()) { // for every file in the .zim
        if (IsRedirect(entry)) {
            ++redirectCount;
            //if (redirectCount % 1000 == 0) {
                //std::cout << "\033[33m\"#" << redirectCount << ": " << entry.getTitle() << "\" is a redirect\033[0m\n";
            //}
            continue;
        }

        std::string path = entry.getPath();
        // if (idx % 100 == 0 && idx % 1000 != 0)
            // std::cout << "#" << idx << " / " << articleCount << ", path: " << path << "\n";

        ++idx;
        if (idx % 1000 == 0) { // commit every 1000
            // std::cout << "\033[34m#" << idx << " / " << articleCount << ": " << entry.getTitle() << ", path: " << path << "\033[0m\n";
        }

        Parser::Parse(archive, path, entry);
    }
}

bool IsRedirect(const zim::Entry& entry) {
    return entry.isRedirect(); // should also correctly handle <meta http-equiv="refresh" ...> which is a redirect
}
