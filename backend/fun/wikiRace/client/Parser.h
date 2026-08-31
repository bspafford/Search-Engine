#pragma once

#include <string>
#include <mutex>
#include <unordered_map>
#include <filesystem>
#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>
#include <zim/archive.h>
#include <atomic>

class ThreadPool;

class Parser {
public:
    static void InitPool(int threads);
    void Init();
    ~Parser();

    static void Parse(const zim::Archive& archive, const std::string& path, const zim::Entry& entry);

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

    // if redirectsTo != null: will follow the line of redirects until it finds a valid page. If false, it will just return if an empty string if the current page isn't a redirect
    //      true: if you want to find where the redirect leads to
    //      false: just if you want to know if the path is a redirect or not
    // function will normalize path, this includes decoding (e.g.: '%23' into '#')
    static bool IsRedirect(const zim::Archive& archive, lxb_html_document_t* document, lxb_dom_collection_t* collection, std::string& path, std::string* redirectsTo);

private:
    void ParsePage(const zim::Archive& archive, std::string path, const zim::Entry& entry);
    void DownloadThumbnail(const zim::Archive& archive, lxb_html_document_t* document, lxb_dom_collection_t* collection, long id, const std::string& path);
    void ExtractText(lxb_dom_node_t* node, std::string& out);
    void GetEmbeddings(lxb_html_document_t* document, lxb_dom_collection_t* collection, long id, const std::string& title);
    std::string FirstChunk(const std::string& text, size_t max_chars = 800);
    void AddURL(long currId, long id);

    static void NormalizeRedirectPath(std::string& path);

    static int hex(char c);
    static std::string urlDecode(const std::string& input);
    //std::string Base64Encode(const uint8_t* data, size_t len);

    static void CleanParseFile(lxb_html_document_t* document, lxb_dom_collection_t* collection);

    lxb_html_document_t* document = nullptr;
    lxb_dom_collection_t* collection = nullptr;
    lxb_html_document_t* redirectDocument = nullptr;
    lxb_dom_collection_t* redirectCollection = nullptr;

    static long GetId(const std::string& path, const std::string& debugFrom, bool* hasParsed);
    static std::string GetPath(const long id);

    // path, { id, hasParsed }
    static inline std::unordered_map<std::string, std::pair<long, bool>> idMap;
    // id, { path, hasParsed }
    static inline std::unordered_map<long, std::pair<std::string, bool>> pathMap;

    static inline ThreadPool* threadPool = nullptr;

    static inline std::mutex idMutex;
    static inline std::mutex pathMutex;

    static inline std::atomic<long> idx = 0;
    static inline std::atomic<long> wikiCount = 0;

    static void InsertRedirect(long from, long to);
    static bool GetRedirect(long from, long* to);
    static inline std::mutex redirectMutex;
    static inline std::unordered_map<long, long> redirectMap;
};
