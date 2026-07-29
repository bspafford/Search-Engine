#pragma once

#include <string>
#include <pqxx/pqxx>
#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>

namespace Indexer {
    void Init(pqxx::connection& cx);
    // returns the ID to the url
    void ExtractKeywords(pqxx::connection& cx, long urlId, const std::string& url, lxb_html_document_t* document, lxb_dom_collection_t *collection);
    void CleanUp();
    float GetSimilarity(const std::string& input1, const std::string& input2);
}
