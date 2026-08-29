#include "WikiParser.h"
#include "ThreadPool.h"
#include "WikiDatabase.h"
#include "helper.h"

#include <iostream>
#include <fstream>
#include <zim/item.h>

void Parser::InitPool(int threads) {
    threadPool = new ThreadPool(threads);

    wikiCount = Database::GetWikiCount();
    idMap.reserve(wikiCount);
    pathMap.reserve(wikiCount);
    std::cout << "IDs: " << wikiCount << "\n";

    for (pqxx::row_ref row : Database::GetIdAndPathFromWiki()) {
        long id = row[0].as<long>();
        std::string path = row[1].as<std::string>();
        bool hasParsed = row[2].as<bool>();
        pathMap.emplace(id, std::pair(path, hasParsed));
        idMap.emplace(std::move(path), std::pair(id, hasParsed));
    }
}

void Parser::Init() {
    std::cout << "init parser\n";

    document = lxb_html_document_create();
    if (document == NULL)
        printf("Document is NULL\n");

    collection = lxb_dom_collection_make(&document->dom_document, 128);
    if (collection == NULL)
        throw std::runtime_error("Collection is NULL\n");
}

Parser::~Parser() {
    std::cout << "destroying parser\n";

    if (document) {
        lxb_html_document_destroy(document);
        document = nullptr;
    }

    if (collection) {
        lxb_dom_collection_destroy(collection, true);
        collection = nullptr;
    }
}

void Parser::Parse(const std::string& thumbnailsPath, const zim::Archive& archive, const std::string& path, const zim::Entry& entry) {
    threadPool->Enqueue([&thumbnailsPath, &archive, path = std::move(path), entry = std::move(entry)] {
        Parser& parser = Parser::GetParser();
        parser.ParsePage(thumbnailsPath, archive, path, entry);
    });
}

void Parser::ParsePage(const std::string& thumbnailsPath, const zim::Archive& archive, const std::string& path, const zim::Entry& entry) {
    lxb_dom_element_t *element;
    lxb_url_t *base_url, *url;
    const lxb_char_t *href, *rel;
    size_t href_len;

    bool hasParsed = false;
    long id = GetId(path, path, &hasParsed);

    // check to see if has already been parsed
    // if program crashes, etc, the pages aren't going to update, so dont check them again
    if (hasParsed) {
        ++idx;
        if (idx % 1000 == 0)
            printf("\033[33m#%ld / %ld, already parsed: %s\033[0m\n", idx.load(), wikiCount.load(), path.c_str());
        return;
    }

    std::string contents = entry.getItem().getData();

    lxb_status_t status = lxb_html_document_parse(document, reinterpret_cast<const lxb_char_t*>(contents.c_str()), contents.size());
    if (status != LXB_STATUS_OK)
        printf("status is not OK: lxb_html_document_parse");

    if (id == -1) {
        std::cerr << "Invalid path id for \"" << id << "\"\n";
        CleanParseFile(document, collection);
        return;
    }

    DownloadThumbnail(thumbnailsPath, archive, document, collection, id, path);

    // Find all <a> elements
    status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->body), collection, (const lxb_char_t *) "a", 1);
    if (status != LXB_STATUS_OK)
        printf("status is not OK 1.\n");

    // Iterate links, extract href, and resolve each URL
    size_t count = lxb_dom_collection_length(collection);
    for (size_t i = 0; i < count; i++) {
        element = lxb_dom_collection_element(collection, i);

        href = lxb_dom_element_get_attribute(element, (const lxb_char_t *) "href", 4, &href_len);
        if (href == NULL) {
            // std::cout << "href is null\n";
            continue;
        }

        std::string p(reinterpret_cast<const char*>(href), href_len);
        long localId = GetId(p, path, nullptr);
        if (localId != -1)
            Database::AddConnection(id, localId);
    }

    Database::SetHasParsed(id, true);

    ++idx;
    if (idx % 100 == 0)
        printf("#%ld / %ld | %ld thread size | %s\n", idx.load(), wikiCount.load(), threadPool->GetQueueSize(), path.c_str());

    CleanParseFile(document, collection);
}

// Downloads the image, hashes the path name, and uploads that value to the database
void Parser::DownloadThumbnail(const std::filesystem::path& thumbnailsPath, const zim::Archive& archive, lxb_html_document_t* document, lxb_dom_collection_t* collection, long id, const std::string& path) {
    lxb_status_t status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->body), collection, reinterpret_cast<const lxb_char_t*>("img"), 3);
    if (status != LXB_STATUS_OK)
        printf("Status for finding IMG is not ok\n");

    size_t count = lxb_dom_collection_length(collection);
    if (count == 0) {
        CleanParseFile(nullptr, collection); // dont clean document
        return;
    }

    lxb_dom_element_t* imgElement = lxb_dom_collection_element(collection, 0);

    // get src attribute
    size_t len = 0;
    const lxb_char_t* src = lxb_dom_element_get_attribute(imgElement, reinterpret_cast<const lxb_char_t*>("src"), 3, &len);
    if (src == nullptr) { // src was not found
        CleanParseFile(nullptr, collection); // dont clean document
        return;
    }

    std::string imgSrc(reinterpret_cast<const char*>(src), len);
    std::string tempPreNorm = imgSrc;
    NormalizeImgSrc(imgSrc);

    try {
        zim::Entry imgEntry = archive.getEntryByPath(imgSrc);

        // get filename hash
        std::string hash = Helper::Hash(path);

        // update hash in DB
        Database::AddImgHash(id, hash);

        // save file to computer
        std::ofstream img(thumbnailsPath / hash, std::ios::binary);
        if (!img.is_open()) {
            CleanParseFile(nullptr, collection); // dont clean document
            return;
        }

        zim::Blob data = imgEntry.getItem().getData();
        img.write(reinterpret_cast<const char*>(data.data()), data.size());

        img.close();
        CleanParseFile(nullptr, collection); // dont clean document

    } catch (const std::exception& e) {
        std::cout << "\033[31mFailed: \"" << imgSrc << "\" from \"" << path << "\" (" << id << ")\033[0m\n";
        std::cout << "pre normalization: " << tempPreNorm << "\n";
        std::cout << e.what() << "\n";
        // throw std::runtime_error("Bad Img Src");
    }
}

void Parser::CleanParseFile(lxb_html_document_t* document, lxb_dom_collection_t* collection) {
    // Cleanup
    if (collection)
        lxb_dom_collection_clean(collection);
    if (document)
        lxb_html_document_clean(document);
}

long Parser::GetId(const std::string& path, const std::string& debugFrom, bool* hasParsed) {
    std::lock_guard<std::mutex> lock(idMutex);

    auto it = idMap.find(path);
    if (it != idMap.end()) { // cached in map
        if (hasParsed) *hasParsed = it->second.second;
        return it->second.first;
    }

    // std::cout << "Path was not in Map: \"" << path << "\"\nComing from: \"" << debugFrom << "\"\n";
    return -1;
}

// path decoder
// e.g. '%2C' -> ','
int Parser::hex(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string Parser::urlDecode(const std::string& input) {
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

void Parser::NormalizeImgSrc(std::string& path) {
    while(path.starts_with("../"))
        path.erase(path.begin(), path.begin() + 3);
    if (path.starts_with("./"))
        path.erase(path.begin(), path.begin() + 2);

    path = urlDecode(path);
}
