#pragma once

#include "helper.h"

#include <string>
#include <pqxx/pqxx>
#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>

namespace Indexer {
    void Init();
    // returns the ID to the url
    void ExtractKeywords(const std::string& url, lxb_html_document_t* document, lxb_dom_collection_t *collection, std::vector<WordData>& words);
    void CleanUp();
    float GetSimilarity(const std::string& input1, const std::string& input2);
}
