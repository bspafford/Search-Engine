#include "login.h"
#include "helper.h"
#include "WikiParser.h"
#include "WikiDatabase.h"
#include "ThreadPool.h"

#include <iostream>
#include <queue>
#include <unordered_set>
#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>
#include <pqxx/pqxx>
#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/html/interfaces/document.h>
#include <zim/archive.h>
#include <zim/entry.h>
#include <zim/item.h>

pqxx::connection cx("host=localhost dbname=wikirace user=" + USER + " password=" + PASSWORD);
std::unordered_map<std::filesystem::path, long> idMap;
std::unordered_map<long, std::filesystem::path> pathMap;
llama_model* model = nullptr;
llama_context* ctx = nullptr;
const llama_vocab* vocab = nullptr;

static lxb_status_t callback(const lxb_char_t *data, size_t len, void *ctx) {
    std::string* str = static_cast<std::string*>(ctx);
    str->append(reinterpret_cast<const char*>(data), len);
    return LXB_STATUS_OK;
}

void ExecuteSQL(pqxx::work& tx, const std::string& path, const std::string& title) {
    // start a transaction
    tx.exec(pqxx::prepped("insert_page"), pqxx::params(path, title));
}

long GetId(const std::string& path, const std::string& debugFrom) {
    auto it = idMap.find(path);
    if (it != idMap.end()) // cached in map
        return it->second;

    // std::cout << "Path was not in Map: \"" << path << "\"\nComing from: \"" << debugFrom << "\"\n";
    return -1;
}

bool IsRedirect(const zim::Entry& entry) {
    return entry.isRedirect(); // should also correctly handle <meta http-equiv="refresh" ...> which is a redirect
}

// Parses the document, gets the thumbnail image and links
void BuildConnections(const std::filesystem::path& zimPath, const std::string& thumbnailsPath) {
    if (!std::filesystem::exists(zimPath) || std::filesystem::is_directory(zimPath)) {
        std::cout << "Invalid path: \"" << zimPath << "\"\n";
        return;
    }

    zim::Archive archive(zimPath);

    long articleCount = archive.getArticleCount();
    std::cout << "Articles: " << articleCount << "\n";

    long idx = 0;
    long redirectCount = 0;
    for(auto& entry : archive.iterByTitle()) {
        if (IsRedirect(entry)) { 
            ++redirectCount;
            if (redirectCount % 1000 == 0) {
                // std::cout << "\033[33m\"#" << redirectCount << ": " << entry.getTitle() << "\" is a redirect\033[0m\n";
            }
            continue;
        }

        std::string path = entry.getPath();
        // if (idx % 100 == 0 && idx % 1000 != 0)
            // std::cout << "#" << idx << " / " << articleCount << ", path: " << path << "\n";

        ++idx;
        if (idx % 1000 == 0) { // commit every 1000
            // std::cout << "\033[34m#" << idx << " / " << articleCount << ": " << entry.getTitle() << ", path: " << path << "\033[0m\n";
        }

        Parser::Parse(thumbnailsPath, archive, path, entry);
    }
}

void GetShortestPathBruteForce(const std::string& startPath, const std::string& endPath) {
    pqxx::work tx{cx};
    std::unordered_set<long> visited;

    long endId = GetId(endPath, endPath);
    if (endId == -1) {
        std::cout << "Invalid endId for: \"" << endPath << "\"\n";
        return;
    }

    std::queue<std::pair<long, std::vector<long>>> q;
    long startId = GetId(startPath, endPath);
    if (endId == -1) {
        std::cout << "Invalid startId for: \"" << startPath << "\"\n";
        return;
    }

    std::cout << "startId: " << startId << ", endId: " << endId << "\n";

    long searchNum = 0;
    q.push({ startId, { startId } });
    while (!q.empty()) {
        auto [id, path] = q.front();
        q.pop();

        // Already searched page, dont search again
        if (visited.find(id) != visited.end())
            continue;

        visited.insert(id);

        for (auto row : tx.exec(pqxx::prepped("select_all_start_where_id"), pqxx::params(id))) {
            ++searchNum;

            long fromId = row[0].as<long>();
            long toId = row[1].as<long>();
            std::string fromPath = row[2].as<std::string>();
            std::string toPath = row[3].as<std::string>();

            std::cout << "#" << searchNum << ", from: " << fromPath << " (" << fromId << "), to: " << toPath << " (" << toId << "), depth: " << path.size() << "\n";

            if (toId == endId) {
                std::cout << "Path was found! Numer of Clicks: " << path.size() << ", and searched: " << searchNum << ", the path was:\n";
                path.push_back(endId); // just for visuals
                for (long p : path)
                    std::cout << pathMap[p] << " (" << p << ")\n";
                return;
            }

            std::vector<long> p = path;
            p.push_back(toId);
            q.push({ toId, std::move(p) });
        }
    }

    tx.commit();
    std::cout << "Invalid, could not find \"" << endPath << "\" starting from \"" << startPath << "\"\n";
}

void OutputPath(long startId, long endId, long fromId, long toId, const std::unordered_map<long, long>& start, const std::unordered_map<long, long>& end) {
    std::cout << "met at: " << fromId << ", ended at: " << toId << "\n";

    std::vector<long> path;

    auto it = start.find(fromId);
    path.push_back(fromId);
    while (it != start.end()) {
        if (it->second == startId) {
            path.push_back(startId);
            break;
        }
        path.push_back(it->second);
        it = start.find(it->second);
    }

    std::reverse(path.begin(), path.end());

    it = end.find(toId);
    path.push_back(toId);
    while (it != end.end()) {
        if (it->second == endId) {
            path.push_back(it->second);
            break;
        }
        path.push_back(it->second);
        it = end.find(it->second);
    }

    std::cout << "\nPath (" << path.size() - 1 << " clicks):\n";
    for (long id : path)
        std::cout << pathMap[id] << " (" << id << ")\n";
}

void GetShortestPath(const std::string& startPath, const std::string& endPath) {
    // Example Path:
    // wiki/Search_engine
    // wiki/Computer_file
    // wiki/NTFS
    // wiki/Microsoft
    // wiki/Nokia
    // wiki/New_York_Stock_Exchange
    // wiki/Mutual_fund

    // LGM-30_Minuteman -> Poison_dart_frog (one-way search: 5,280,297 searches, depth 4)
        // removed duplicates: 1,343,282
        // multidirectional search: 480

    // "LGM-30_Minuteman" (3326125)
    // "United_States_Air_Force" (3314494)
    // "Gold_(color)" (2946006)
    // "Animal" (2811705)
    // "Poison_dart_frog" (2638956)

    // Other Random Ideas:
    // cpu instruction set
    // search engine
    // Evolution

    pqxx::work tx{cx};

    long endId = GetId(endPath, endPath);
    if (endId == -1) {
        std::cout << "Invalid endId for: \"" << endPath << "\"\n";
        return;
    }

    long startId = GetId(startPath, endPath);
    if (endId == -1) {
        std::cout << "Invalid startId for: \"" << startPath << "\"\n";
        return;
    }

    std::cout << "startId: " << startId << ", endId: " << endId << "\n";

    // start -> ...
    // ... <- end
    // look towards eachother, maybe add to set, if item reached from start in endSet, you got path
    // else if item reached from end in startSet, you got path
    // do one full depth at a time

    // each end and start:
    // queue, set (or map)

    long searchNum = 0;
    std::queue<long> sq, eq;
    // id, parent id
    std::unordered_map<long, long> ss, es;
    sq.push(startId);
    eq.push(endId);
    while (!sq.empty() || !eq.empty()) {
        if (!sq.empty()) {
            auto sId = sq.front();
            sq.pop();
            const pqxx::result sRes = tx.exec(pqxx::prepped("select_all_start_where_id"), pqxx::params(sId));
            for (auto row : sRes) { // do Start side, until next depth
                ++searchNum;

                long fromId = row[0].as<long>();
                long toId = row[1].as<long>();
                std::string fromPath = row[2].as<std::string>();
                std::string toPath = row[3].as<std::string>();
                auto [_, inserted] = ss.emplace(toId, fromId);
                if (!inserted)
                    continue;

                // std::cout << "#" << searchNum << ", start from: " << fromPath << " (" << fromId << "), to: " << toPath << " (" << toId << "), depth: " << sPath.size() << "\n";
                std::cout << "#" << searchNum << ", start from: " << fromPath << " (" << fromId << "), to: " << toPath << " (" << toId << "), depth: \n";

                auto it = es.find(toId); // if already in map
                if (it != es.end() || toId == endId) { // path was found
                    // std::cout << "Path was found! Numer of Clicks: " << sPath.size() << ", and searched: " << searchNum << ", the path was:\n";
                    std::cout << "\nPath was found! Numer of Clicks: , and searched: " << searchNum << ", the path was:\n";

                    OutputPath(startId, endId, fromId, toId, ss, es);

                    return;
                }

                sq.push(toId);
            }
        }

        // do End side, until next depth
        if (!eq.empty()) {
            auto eId = eq.front();
            eq.pop();
            const pqxx::result eRes = tx.exec(pqxx::prepped("select_all_end_where_id"), pqxx::params(eId));
            for (auto row : eRes) {
                ++searchNum;

                long fromId = row[0].as<long>();
                long toId = row[1].as<long>();
                std::string fromPath = row[2].as<std::string>();
                std::string toPath = row[3].as<std::string>();
                auto [_, inserted] = es.emplace(fromId, toId);
                if (!inserted)
                    continue;

                // std::cout << "#" << searchNum << ", end from: " << fromPath << " (" << fromId << "), to: " << toPath << " (" << toId << "), depth: " << ePath.size() << "\n";
                std::cout << "#" << searchNum << ", end from: " << fromPath << " (" << fromId << "), to: " << toPath << " (" << toId << "), depth: " << "\n";

                auto it = ss.find(fromId); // if already in map
                if (it != ss.end() || fromId == startId) { // path was found
                    // std::cout << "Path was found! Numer of Clicks: " << ePath.size() << ", and searched: " << searchNum << ", the path was:\n";
                    std::cout << "\nPath was found! Numer of Clicks: " << ", and searched: " << searchNum << ", the path was:\n";

                    OutputPath(startId, endId, fromId, toId, ss, es);
                    return;
                }

                eq.push(fromId);
            }
        }
    }

    tx.commit();
    std::cout << "Invalid, could not find \"" << endPath << "\" starting from \"" << startPath << "\"\n";
}

void ConnectToDB() {
    std::cout << "Main Connected to " << cx.dbname() << "\n";

    cx.prepare(
        "insert_page",
        "INSERT INTO wiki(path, title, authority) "
        "VALUES($1, $2, 0) " // set authority to 0 when first making page, so count doesn't go to infinity
        "ON CONFLICT (path) "
        "DO UPDATE SET "
        "path = EXCLUDED.path, "
        "title = EXCLUDED.title"
    );

    cx.prepare(
        "GetId",
        "SELECT id FROM wiki WHERE path = $1"
    );

    cx.prepare(
        "select_all_start_where_id",
        // "SELECT * FROM connections WHERE id = $1"
        "SELECT c.id, c.connection, wf.path, wt.path "
        "FROM connections c "
        "JOIN wiki wf ON c.id = wf.id "
        "JOIN wiki wt ON c.connection = wt.id "
        "WHERE c.id = $1"
    );

    cx.prepare(
        "select_all_end_where_id",
        // "SELECT * FROM connections WHERE id = $1"
        "SELECT c.id, c.connection, wf.path, wt.path "
        "FROM connections c "
        "JOIN wiki wf ON c.id = wf.id "
        "JOIN wiki wt ON c.connection = wt.id "
        "WHERE c.connection = $1"
    );

    cx.prepare(
        "getNullEmbeddings",
        "SELECT * FROM wiki WHERE embedding IS NULL OR vector_dims(embedding) = 0"
    );

    cx.prepare(
        "UpdateEmbedding",
        "UPDATE wiki SET embedding = $1 WHERE id = $2"
    );
}

void SetEmbeddings() {
    Helper::InitEmbedModel(model, ctx, vocab);

    // select id, title FROM ... where embeddings = null
    // convert title to embedding
    // update embedding where id = id
    pqxx::work tx{cx};
    pqxx::result result = tx.exec(pqxx::prepped("getNullEmbeddings"));
    tx.commit();

    std::cout << "how many results: " << result.size() << "\n";

    long count = 0;
    for (pqxx::row_ref row : result) {
        ++count;

        std::string title = row["title"].as<std::string>();
        if (title.empty())
            title = row["path"].as<std::string>();
        
        std::cout << "#" << count << "/" << result.size() << ": " << title << "\n";

        std::vector<float> embedding = Helper::EmbedText(model, ctx, vocab, title);
        std::string sql = "UPDATE wiki SET embedding = '[";
        for (size_t i = 0; i < embedding.size(); ++i) {
            if (i != 0) sql += ",";
            sql += std::to_string(embedding[i]);
        }
        sql += "]'::vector WHERE id = $1";

        pqxx::work tx{cx};
        tx.exec(sql, pqxx::params(row["id"].as<long>()));
        tx.commit();
    }
}

// outputs all items in db
void OutputWikiToFile() {
    std::ofstream file("./temp.txt");
    if (!file.is_open()) {
        std::cout << "couldn't open file\n";
        return;
    }

    std::string sql = "SELECT * FROM wiki";

    pqxx::work tx{cx};
    pqxx::result result = tx.exec(sql);
    tx.commit();

    for (pqxx::row_ref row : result) {
        file << row["id"].as<std::string>() << "," << row["path"].as<std::string>() << "," << row["title"].as<std::string>() << "\n";
    }

    file.close();
}

void InputEmbeddingsFromFile() {
    std::ifstream file("./output.txt", std::ios::binary);
    if (!file.is_open()) {
        std::cout << "couldn't open file\n";
        return;
    }

    constexpr uint32_t embeddingSize = 768;
    constexpr size_t recordSize = sizeof(uint64_t) + sizeof(uint32_t) + embeddingSize * sizeof(float);

    // get file size in bytes
    file.seekg(0, std::ios::end);
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    size_t recordCount = fileSize / recordSize;

    long idx = 0;
    pqxx::work tx{cx};
    while (file.peek() != EOF) {
        ++idx;

        uint64_t id = 0;
        file.read(reinterpret_cast<char*>(&id), sizeof(id));

        uint32_t count = 0;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));

        std::vector<float> embedding(count);
        file.read(reinterpret_cast<char*>(embedding.data()), embedding.size() * sizeof(float));

        std::string embeddingStr = "[";
        for (size_t i = 0; i < embedding.size(); ++i) {
            if (i != 0) embeddingStr += ",";
            embeddingStr += std::to_string(embedding[i]);
        }
        embeddingStr += "]";

        tx.exec(pqxx::prepped("UpdateEmbedding"), pqxx::params(embeddingStr, id));

        if (idx % 1000 == 0) {
            float percent = idx / (float)recordCount * 100.f;
            printf("\r#%ld / %ld (%f)", idx, recordCount, percent);
            tx.commit();
        }
    }
    tx.commit();

    std::cout << "\n";
    file.close();
}

void BuildWikiDB(std::string zimPath) {
    pqxx::work tx{cx};

    zim::Archive archive(zimPath);

    long articleCount = archive.getArticleCount();
    std::cout << "Articles: " << articleCount << "\n";

    long idx = 0;
    long redirectCount = 0;
    for(auto& entry : archive.iterByTitle()) {
        ++idx;

        if (IsRedirect(entry)) { // should also correctly handle <meta http-equiv="refresh" ...> which is a redirect
            // std::cout << "\033[31m\"" << entry.getTitle() << "\" is a redirect\033[0m\n";
            ++redirectCount;
            if (redirectCount % 1000 == 0)
                std::cout << "\033[33m\"#" << redirectCount << ": " << entry.getTitle() << "\" is a redirect\033[0m\n";
            continue;
        }

        // std::cout << "\033[33mtitle: \"" << entry.getTitle() << "\"\033[0m\n";
        // zim::Item item = entry.getItem();
        // zim::Blob blob = item.getData();
        // std::string html(reinterpret_cast<const char*>(blob.data()), blob.size());

        std::string path = entry.getPath();//lexically_relative(path);

        ExecuteSQL(tx, path, entry.getTitle());

        if (idx % 1000 == 0) { // commit every 1000
            std::cout << "\033[34m#" << idx << " / " << articleCount << ": " << entry.getTitle() << ", path: " << path << "\033[0m\n";
            tx.commit();
        }
    }
    tx.commit();
}

int main(int argc, char* argv[]) {
    ConnectToDB();
    // OutputWikiToFile();
    // InputEmbeddingsFromFile();
    // return 0;
    std::string tempPath = "/home/ben/.local/share/kiwix-desktop/wikipedia_en_top1m_maxi_2026-04.zim"; // temp
    std::string basePath = "/media/ben/902a7657-0ecf-43a9-bb62-efbb62280115/wikiThumbnails";

    // download image
    // parse file
    // entry.gettitle.getdata()?

    Database::InitPool(10);
    Parser::InitPool(10);

    /*
    std::string str = "0c70a452f799bfe840676ee341124611/38-45_clerke_metallic.jpg";
    zim::Archive archive(tempPath);
    for (auto& entry : archive.iterByPath()) {
        std::string path = entry.getPath();
        if (path.find(str) != std::string::npos)
            std::cout << path << "\n";
    }
    return 0;
    */
    /*
    zim::Archive archive(tempPath);
    std::string str = "../_assets_/c8f24dc75f9c782269c846c9b17e400f/.hackGULogo.png";
    Parser::NormalizeImgSrc(str);
    std::cout << "str: " << str << "\n";
    zim::Entry entry = archive.getEntryByPath(str);
    std::cout << entry.getTitle() << "\n";
    return 0;
    */

    std::cout << "building connections\n";
    BuildConnections(tempPath, basePath);
    ThreadPool::Wait();

    return 0;

    // path
    if (argc == 2) { // ./build/fun/wikiRace/wikiRace ~/Documents/wiki
        std::cout << "Building Whole DB\n";
        BuildWikiDB(argv[1]);
        BuildConnections(argv[1], basePath);
    } else if (argc == 3 && std::strcmp(argv[1], "wiki") == 0) {
        std::cout << "Building Only Wiki Part\n";
        BuildWikiDB(argv[2]);
    } else if (argc == 3 && std::strcmp(argv[1], "conn") == 0) {
        std::cout << "Building Only Connections Part\n";
        BuildConnections(argv[2], basePath);
    } else if (argc == 3 && std::strcmp(argv[1], "embed") == 0) {
        // loop through all items that have null for their embeddings, then update them
        SetEmbeddings();
    } else if (argc == 3) {
        std::cout << "Finding Shortest Path\n";
        GetShortestPath(argv[1], argv[2]);
        // GetShortestPath("African_lions", "Evolution");
    } else {
        throw std::runtime_error("Invalid args");
    }

    ThreadPool::Wait();
}
