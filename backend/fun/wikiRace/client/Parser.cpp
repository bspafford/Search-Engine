#include "Parser.h"
#include "ThreadPool.h"
#include "Database.h"
#include "Model.h"
#include "Helper.h"

#include <iostream>
#include <string>
#include <fstream>
#include <zim/item.h>
#include <sstream>

void Parser::InitPool(int threads) {
    threadPool = new ThreadPool(threads);

    nlohmann::json data = Database::GetIdAndPathFromWiki();
    idMap.reserve(data.size());
    pathMap.reserve(data.size());
    std::cout << "DB Size: " << data.size() << "\n";
    wikiCount = data.size();

    size_t insertedPath = 0;
    size_t insertedId = 0;
    for (nlohmann::json& row : data) {
        long id = row["id"].get<long>();
        std::string path = row["path"].get<std::string>();
        bool hasParsed = row["hasParsed"].get<bool>();
        auto [it1, inserted1] = pathMap.emplace(id, std::pair(path, hasParsed));
        auto [it2, inserted2] = idMap.emplace(std::move(path), std::pair(id, hasParsed));
        insertedPath += inserted1;
        insertedId += inserted2;
        if (!inserted1)
            std::cout << "duplicate id: " << id << ", json id: " << row["id"] << ", existing path: " << path << ", new path: " << row["path"] << "\n";
        if (!inserted2)
            std::cout << "duplicate path: " << id << ", json id: " << row["id"] << ", existing path: " << path << ", new path: " << row["path"] << "\n";
    }

    printf("insertedPath: %s, insertedId: %s\n", Helper::PrettyPrint(insertedPath), Helper::PrettyPrint(insertedId));
    printf("Size: %s, %s\n", Helper::PrettyPrint(idMap.size()), Helper::PrettyPrint(pathMap.size()));
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

void Parser::Parse(const zim::Archive& archive, const std::string& path, const zim::Entry& entry) {
    //threadPool->Enqueue([&archive, path = std::move(path), entry = std::move(entry)] {
    threadPool->Enqueue([&archive, path, entry] {
        Parser& parser = Parser::GetParser();
        parser.ParsePage(archive, path, entry);
    });
}

void Parser::ParsePage(const zim::Archive& archive, const std::string& path, const zim::Entry& entry) {
    lxb_dom_element_t *element;
    lxb_url_t *base_url, *url;
    const lxb_char_t *href, *rel;
    size_t href_len;

    bool hasParsed = false;
    long id = GetId(path, path, &hasParsed);

    if (id == -1) {

        std::stringstream ss;
        std::cerr << "Bytes: ";
        for (unsigned char c : path) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c) << ' ';
        }
        ss << std::dec << '\n';

        printf("\033[31mInvalid path id for \"\033[0m%s\033[31m\"\033[0m\nbytes: %s\n", path.c_str(), ss.str());

        CleanParseFile(document, collection);
        return;
    }

    // check to see if has already been parsed
    // if program crashes, etc, the pages aren't going to update, so dont check them again
    if (hasParsed) {
        ++idx;
        if (idx % 1000 == 0)
            printf("\033[33m#%s / %s, already parsed: %s\033[0m\n", Helper::PrettyPrint(idx.load()), Helper::PrettyPrint(wikiCount.load()), path.c_str());
        return;
    }

    std::string contents = entry.getItem().getData();

    lxb_status_t status = lxb_html_document_parse(document, reinterpret_cast<const lxb_char_t*>(contents.c_str()), contents.size());
    if (status != LXB_STATUS_OK)
        printf("status is not OK: lxb_html_document_parse");

    DownloadThumbnail(archive, document, collection, id, path);
    GetEmbeddings(document, collection, id, entry.getTitle());

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
        printf("#%s / %s: %s\n", Helper::PrettyPrint(idx.load()), Helper::PrettyPrint(wikiCount.load()), path.c_str());

    CleanParseFile(document, collection);
}

// Downloads the image, hashes the path name, and uploads that value to the database
void Parser::DownloadThumbnail(const zim::Archive& archive, lxb_html_document_t* document, lxb_dom_collection_t* collection, long id, const std::string& path) {
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

        zim::Blob blob = imgEntry.getItem().getData();

        std::vector<uint8_t> data(blob.data(), blob.data() + blob.size());
        Database::AddImgPath(id, path, data);

    } catch (const std::exception& e) {
        std::cout << "\033[31mFailed: \"" << imgSrc << "\" from \"" << path << "\" (" << id << ")\033[0m\n";
        std::cout << "pre normalization: " << tempPreNorm << "\n";
        std::cout << e.what() << "\n";
        // throw std::runtime_error("Bad Img Src");
    }

    CleanParseFile(nullptr, collection); // dont clean document
}

void Parser::ExtractText(lxb_dom_node_t* node, std::string& out) {
    for (auto* child = lxb_dom_node_first_child(node); child; child = lxb_dom_node_next(child)) {
        if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            size_t len;
            lxb_char_t* textContent = lxb_dom_node_text_content(child, &len);
            if (textContent && len > 0) {
                out.append(reinterpret_cast<const char*>(textContent), len);
                lxb_dom_document_destroy_text(node->owner_document, textContent);
            }
        } else if (child->type == LXB_DOM_NODE_TYPE_ELEMENT)
            ExtractText(child, out);
    }
}

void Parser::GetEmbeddings(lxb_html_document_t* document, lxb_dom_collection_t* collection, long id, const std::string& title) {
    lxb_dom_element_t* element;
    lxb_status_t status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->body), collection, (const lxb_char_t*)"p", 1);
    if (status != LXB_STATUS_OK)
        printf("status is not OK 1.\n");

    std::string text;
    // Iterate p tag
    size_t count = lxb_dom_collection_length(collection);
    for (size_t i = 0; i < count; i++) {
        element = lxb_dom_collection_element(collection, i);

        ExtractText(&element->node, text);
    }

    std::string description = FirstChunk(text);
    std::string embedStr = "title: " + title + "\ndescription: " + description;
    //printf("EmbedStr: %s\n", embedStr.c_str());

    Model::EmbedText(id, embedStr);

    CleanParseFile(nullptr, collection); // dont clean document
}

std::string Parser::FirstChunk(const std::string& text, size_t max_chars) {
    if (text.size() <= max_chars)
        return text;

    size_t pos = text.find_last_of(".!?", max_chars);

    if (pos == std::string::npos)
        return text.substr(0, max_chars);

    return text.substr(0, pos + 1);
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
        //printf("\033[32mPath Found: \"\033[33m%s\033[32m\"\n", path.c_str());
        if (hasParsed) *hasParsed = it->second.second;
        return it->second.first;
    }

    //printf("Path was not in Map: \"%s\"\nComing from: \"%s\"%ld\n", path.c_str(), debugFrom.c_str(), idMap.size());
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
    while(path._Starts_with("../"))
        path.erase(path.begin(), path.begin() + 3);
    if (path._Starts_with("./"))
        path.erase(path.begin(), path.begin() + 2);

    path = urlDecode(path);
}

/*
static const char base64_chars[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";

std::string Parser::Base64Encode(const uint8_t* data, size_t len) {
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;

        if (i + 1 < len)
            n |= static_cast<uint32_t>(data[i + 1]) << 8;

        if (i + 2 < len)
            n |= static_cast<uint32_t>(data[i + 2]);

        result += base64_chars[(n >> 18) & 63];
        result += base64_chars[(n >> 12) & 63];

        if (i + 1 < len)
            result += base64_chars[(n >> 6) & 63];
        else
            result += '=';

        if (i + 2 < len)
            result += base64_chars[n & 63];
        else
            result += '=';
    }

    return result;
}
*/