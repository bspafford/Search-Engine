#pragma once

#include <string>
#include <unordered_map>
#include <filesystem>
#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>
#include <pqxx/pqxx>
#include <zim/archive.h>

class ThreadPool;

class Parser {
public:
    static void InitPool(int threads);
    void Init();
    ~Parser();

    static void Parse(const std::string& thumbnailsPath, const zim::Archive& archive, const std::string& path, const zim::Entry& entry);

    static Parser& GetParser() {
        thread_local Parser parser;
        thread_local bool initialized = false;
        if (!initialized) {
            parser.Init();
            initialized = true;
        }
        return parser;
    }

    static void NormalizeImgSrc(std::string& path);

private:
    void ParsePage(const std::string& thumbnailsPath, const zim::Archive& archive, const std::string& path, const std::string& contents);
    void DownloadThumbnail(const std::filesystem::path& thumbnailsPath, const zim::Archive& archive, lxb_html_document_t* document, lxb_dom_collection_t* collection, long id, const std::string& path);
    void AddURL(pqxx::work& tx, long currId, long id);

    static int hex(char c);
    static std::string urlDecode(const std::string& input);

    void CleanParseFile(lxb_html_document_t* document, lxb_dom_collection_t* collection);

    lxb_html_document_t* document = nullptr;
    lxb_dom_collection_t* collection = nullptr;

    static long GetId(const std::string& path, const std::string& debugFrom);

    static inline std::unordered_map<std::filesystem::path, long> idMap;
    static inline std::unordered_map<long, std::filesystem::path> pathMap;

    static inline ThreadPool* threadPool = nullptr;

    static inline std::mutex idMutex;
};
