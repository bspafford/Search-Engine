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
#include "lexbor/html/interfaces/document.h"
#include "login.h"
#include "helper.h"

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

// path decoder
// e.g. '%2C' -> ','
int hex(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string urlDecode(const std::string& input) {
    std::string output;
    output.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size() && std::isxdigit(static_cast<unsigned char>(input[i + 1])) && std::isxdigit(static_cast<unsigned char>(input[i + 2]))) {
            int value = (hex(input[i + 1]) << 4) | hex(input[i + 2]);
            output.push_back(static_cast<char>(value));
            i += 2;
        // } else if (input[i] == '/') { // convert '/' to '%2f'
            // output += "%2f";
        } else
            output.push_back(input[i]);
    }

    return output;
}

void ExecuteSQL(pqxx::work& tx, const std::string& path, const std::string& title) {
    // start a transaction
    tx.exec(pqxx::prepped("insert_page"), pqxx::params(path, title));
}

bool NormalizeUrl(std::string& url) {
    size_t pos = url.find('#');
    if (pos != std::string::npos)
        url.erase(pos);

    bool localStart = url.find(':') == std::string::npos && url.find(' ') == std::string::npos; // || url.starts_with("./") || url.starts_with("/");
    if (!localStart)
        return false;

    // remove './' of ./local/path...
    if (url.starts_with("./"))
        url.erase(0, 2);

    url = urlDecode(url);

    // removes files like "_exceptions/"
    pos = url.rfind('/');
    if (pos != std::string::npos)
        url.erase(0, pos + 1);

    return !url.empty();
}

void AddURL(pqxx::work& tx, long currId, long id) {
    // executesql to add the url to some relational db since i dont really need to crawl cause i already know what pages exist

    tx.exec(pqxx::prepped("insert_connection"), pqxx::params(currId, id));
    tx.exec(pqxx::prepped("increaseAuthority"), pqxx::params(id));
}

std::string GetTitle(lxb_html_document_t *document, const std::filesystem::path& basePath, const std::filesystem::path& filePath, const std::string& contents) {
    size_t start = contents.find("<title>");
    if (start == std::string::npos)
        return "";

    size_t end = contents.find("</title>", start);
    if (end == std::string::npos)
        return "";

    return contents.substr(start + 7, end - start - 7);

    /*
    lxb_status_t status = lxb_html_document_parse(document, reinterpret_cast<const lxb_char_t*>(contents.c_str()), contents.size());
    if (status != LXB_STATUS_OK)
        printf("Something went wrong 2.\n");

    // get title
    lxb_dom_collection_t* title = lxb_dom_collection_make(&document->dom_document, 1);
    if (!title)
        printf("title is null");

    status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->head), title, (const lxb_char_t*)"title", 5);
    if (status != LXB_STATUS_OK)
        printf("No title found");

    size_t titleLen;
    lxb_dom_element_t* titleElement = lxb_dom_collection_element(title, 0);
    if (!titleElement) {
        lxb_dom_collection_destroy(title, true);
        lxb_html_document_clean(document);
        return "";
    }

    lxb_char_t* titleChars = lxb_dom_node_text_content(lxb_dom_interface_node(titleElement), &titleLen);
    std::string titleString(reinterpret_cast<const char*>(titleChars));

    lxb_dom_collection_destroy(title, true);
    lxb_html_document_clean(document);
    return titleString;
    */
}

std::string GetEmbedding(lxb_html_document_t *document, const std::filesystem::path& basePath, const std::filesystem::path& filePath, const std::string& contents, const std::string& title) {
    std::cout << "start embedding\n";
    // get title and first few lines
    std::string str = title;// + ". " + intro; // start with only title, add like 1-3 sentences later

    // converted list to string: "[x, y, z, ...]"
    std::vector<float> embedding = Helper::EmbedText(model, ctx, vocab, str);
    std::string embedStr = "[";
    for (size_t i = 0; i < embedding.size(); ++i) {
        if (i != 0)
            embedStr += ",";
        embedStr += std::to_string(embedding[i]);
    }

    std::cout << "finished embedding\n";
    return embedStr + "]";
}

long GetId(pqxx::work& tx, const std::filesystem::path& path, const std::filesystem::path& debugFrom) {
    auto it = idMap.find(path);
    if (it != idMap.end()) // cached in map
        return it->second;

    std::cout << "Path was not in Map: \"" << path.string() << "\"\nComing from: \"" << debugFrom.string() << "\"\n";
    return -1;
}

void CleanParseFile(lxb_html_document_t* document, lxb_dom_collection_t* collection) {
    // Cleanup
    lxb_dom_collection_clean(collection);
    lxb_html_document_clean(document);
}

void ParseFile(pqxx::work& tx, lxb_html_document_t* document, lxb_dom_collection_t* collection, const std::filesystem::path& basePath, const std::filesystem::path& filePath, const std::string& contents) {
    lxb_dom_element_t *element;
    lxb_url_t *base_url, *url;
    const lxb_char_t *href, *rel;
    size_t href_len;

    lxb_status_t status = lxb_html_document_parse(document, reinterpret_cast<const lxb_char_t*>(contents.c_str()), contents.size());
    if (status != LXB_STATUS_OK)
        printf("status is not OK: lxb_html_document_parse");

    // Find all <a> elements
    status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->body), collection, (const lxb_char_t *) "a", 1);
    if (status != LXB_STATUS_OK)
        printf("status is not OK 1.\n");

    std::string localPath = filePath.lexically_relative(basePath).string();
    if (!NormalizeUrl(localPath)) {
        CleanParseFile(document, collection);
        return;
    }

    long pathId = GetId(tx, localPath, localPath);
    if (pathId == -1) {
        std::cerr << "Invalid path id for \"" << localPath << "\"\n";
        CleanParseFile(document, collection);
        return;
    }

    // Iterate links, extract href, and resolve each URL
    size_t count = lxb_dom_collection_length(collection);
    for (size_t i = 0; i < count; i++) {
        element = lxb_dom_collection_element(collection, i);

        rel = lxb_dom_element_get_attribute(element, (const lxb_char_t*)"rel", 3, &href_len);
        if (rel == NULL || std::strcmp(reinterpret_cast<const char*>(rel), "mw:WikiLink") != 0)
            continue;

        href = lxb_dom_element_get_attribute(element,
                                             (const lxb_char_t *) "href", 4,
                                             &href_len);
        if (href == NULL) {
            // printf("[%zu] <a> without href, skipping.\n", i);
            continue;
        }

        std::string path(reinterpret_cast<const char*>(href), href_len);
        if (NormalizeUrl(path)) {
            long localId = GetId(tx, path, localPath);
            if (localId != -1)
                AddURL(tx, pathId, localId);
        }
    }

    CleanParseFile(document, collection);
}

std::string GetFileContents(const std::filesystem::path& filePath) {
    std::ifstream file(filePath);
    if (file) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    throw std::runtime_error("file was not valid");
}

void CrawlFiles(const std::filesystem::path& path, bool firstPass) {
    if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
        std::cout << "Invalid path: \"" << path << "\"\n";
        return;
    }

    // Helper::InitEmbedModel(model, ctx, vocab, "nomic-embed-text-v1.5.Q2_K.gguf");

    pqxx::work tx{cx};

    // Parse the HTML document
    lxb_html_document_t* document = lxb_html_document_create();
    std::string title, filePath;
    if (document == NULL)
        throw std::runtime_error("Document was null");


    lxb_dom_collection_t* collection = nullptr;
    if (!firstPass) {
        collection = lxb_dom_collection_make(&document->dom_document, 128);
        if (collection == NULL)
            throw std::runtime_error("Collection was null");
    }

    long idx = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
        if (entry.is_regular_file()) {
            std::string contents = GetFileContents(entry);
            if (firstPass) {
                title = GetTitle(document, path, entry, contents);
                // embedding = GetEmbedding(document, path, entry, contents, title);
                filePath = entry.path().lexically_relative(path);
                NormalizeUrl(filePath);
                if (idx % 1000 == 0)
                    std::cout << "#" << idx + 1 << ", adding: \"" << filePath << "\", title: \"" << title << "\"\n";
                ExecuteSQL(tx, filePath, title);
            } else { // not first pass
                if (idx % 100 == 0)
                    std::cout << "#" << idx << ", path: " << entry.path().lexically_relative(path) << "\n";
                ParseFile(tx, document, collection, path, entry, contents);
            }

            if (idx % 1000 == 0) // commit every 1000
                tx.commit();
            ++idx;
        }
    }

    // Commit the transaction
    tx.commit();

    lxb_html_document_destroy(document);
    lxb_dom_collection_destroy(collection, true);
}

void BuildWikiDB(const std::filesystem::path& path) {
    CrawlFiles(std::filesystem::path(path), true);
}

void BuildConnections(const std::filesystem::path& path) {
    CrawlFiles(std::filesystem::path(path), false);
}

void GetShortestPathBruteForce(const std::string& startPath, const std::string& endPath) {
    pqxx::work tx{cx};
    std::unordered_set<long> visited;

    long endId = GetId(tx, endPath, endPath);
    if (endId == -1) {
        std::cout << "Invalid endId for: \"" << endPath << "\"\n";
        return;
    }

    std::queue<std::pair<long, std::vector<long>>> q;
    long startId = GetId(tx, startPath, endPath);
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

    long endId = GetId(tx, endPath, endPath);
    if (endId == -1) {
        std::cout << "Invalid endId for: \"" << endPath << "\"\n";
        return;
    }

    long startId = GetId(tx, startPath, endPath);
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
    std::cout << "Connected to " << cx.dbname() << "\n";

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
        "insert_connection",
        "INSERT INTO connections(id, connection) "
        "VALUES($1, $2)"
        "ON CONFLICT (id, connection)"
        "DO NOTHING"
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

    cx.prepare(
        "increaseAuthority",
        "UPDATE wiki "
        "SET authority = authority + 1 "
        "WHERE id = $1"
    );
}

void InitIdMap() {
    pqxx::work tx{cx};
    size_t count = tx.query_value<std::size_t>("SELECT COUNT(*) FROM wiki");
    idMap.reserve(count);
    pathMap.reserve(count);
    std::cout << "IDs: " << count << "\n";

    for (auto [id, path] : tx.query<long, std::string>("SELECT id, path FROM wiki")) {
        pathMap.emplace(id, path);
        idMap.emplace(std::move(path), id);
    }
    tx.commit();
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
void temp() {
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

void outputTemp() {
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

int main(int argc, char* argv[]) {
    // Path was not in Map: "Academy,_Trenton,_New_Jersey" Coming from: "Mercer_County,_New_Jersey"
    /*
    std::string url = "Hanover/Academy,_Trenton,_New_Jersey";
    std::cout << "before: " << url << "\n";
    NormalizeUrl(url);
    std::cout << "after: " << url << "\n";
    return 0;
    */

    ConnectToDB();
    // temp();
    // outputTemp();
    // return 0;

    // path
    if (argc == 2) { // ./build/fun/wikiRace/wikiRace ~/Documents/wiki
        std::cout << "Building Whole DB\n";
        BuildWikiDB(argv[1]);
        InitIdMap();
        BuildConnections(argv[1]);
    } else if (argc == 3 && std::strcmp(argv[1], "wiki") == 0) {
        std::cout << "Building Only Wiki Part\n";
        BuildWikiDB(argv[2]);
    } else if (argc == 3 && std::strcmp(argv[1], "conn") == 0) {
        std::cout << "Building Only Connections Part\n";
        InitIdMap();
        BuildConnections(argv[2]);
    } else if (argc == 3 && std::strcmp(argv[1], "embed") == 0) {
        // loop through all items that have null for their embeddings, then update them
        SetEmbeddings();
    } else if (argc == 3) {
        std::cout << "Finding Shortest Path\n";
        InitIdMap();
        GetShortestPath(argv[1], argv[2]);
        // GetShortestPath("African_lions", "Evolution");
    } else {
        throw std::runtime_error("Invalid args");
    }
}
