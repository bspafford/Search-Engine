#include "indexer.h"
#include "lexbor/html/interface.h"
#include "lexbor/tag/const.h"
#include "llama.h"

#include <iostream>
#include <vector>
#include <string>
#include <unordered_set>
#include <fstream>
#include <cmath>
#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>

// Simple L2 normalization
static void normalize(std::vector<float>& v) {
    float sum = 0.0f;

    for (float x : v)
        sum += x * x;

    float inv = 1.0f / std::sqrt(sum);

    for (float& x : v)
        x *= inv;
}

// Tokenize
static std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text) {
    int count = llama_tokenize(vocab, text.c_str(), text.size(), nullptr, 0, true, true);

    std::vector<llama_token> tokens(-count);

    llama_tokenize(vocab, text.c_str(), text.size(), tokens.data(), tokens.size(), true, true);

    return tokens;
}

// Compute embedding
std::vector<float> embed_text(llama_model* model, llama_context* ctx, const std::string& text) {
    const llama_vocab* vocab = llama_model_get_vocab(model);

    auto tokens = tokenize(vocab, text);

    llama_batch batch = llama_batch_init(tokens.size(), 0, 1);

    for (size_t i = 0; i < tokens.size(); i++) {
        batch.token[batch.n_tokens] = tokens[i];
        batch.pos[batch.n_tokens] = i;
        batch.n_seq_id[batch.n_tokens] = 1;
        batch.seq_id[batch.n_tokens][0] = 0;
        batch.logits[batch.n_tokens] = (i == tokens.size() - 1);
        batch.n_tokens++;
    }

    // Clear KV cache (not needed for embeddings)
    llama_memory_clear(llama_get_memory(ctx), true);

    if (llama_decode(ctx, batch))
        throw std::runtime_error("Embedding failed.");

    const float* emb = llama_get_embeddings_seq(ctx, 0);

    int dim = llama_model_n_embd_out(model);

    std::vector<float> result(emb, emb + dim);

    normalize(result);

    llama_batch_free(batch);

    return result;
}

llama_model* model;
llama_context* ctx;
std::unordered_set<std::string> stopWords;

bool ShouldSkip(lxb_dom_node_t* node) {
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return false;

    uintptr_t name = node->local_name;
    return name == LXB_TAG_SCRIPT ||
           name == LXB_TAG_STYLE ||
           name == LXB_TAG_NOSCRIPT ||
           name == LXB_TAG_SVG ||
           name == LXB_TAG_TEMPLATE;
}

std::string_view FindSplit(const std::string& text, size_t& offset) {
    std::string_view view = text;

    size_t start = offset;
    offset = std::min(view.find(' ', start), view.find('\n', start));
    view = view.substr(start, offset - start);
    if (offset != std::string::npos)
        ++offset;
    return view;
}

bool IsImportantWord(const std::string_view& word) {
    return !stopWords.contains(std::string(word));
}

void ParseText(std::string text, std::unordered_map<std::string, int>& counts) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    // remove all non a-z OR A-Z chars
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) {
        return !std::isalpha(c) && c != ' ';
    }), text.end());

    // parse word by word
    size_t pos = 0;
    while (pos != std::string::npos) {
        std::string_view word = FindSplit(text, pos);
        if (!word.empty() && IsImportantWord(word))
            ++counts[std::string(word)];
    }
}

void Traverse(lxb_dom_node_t* node, std::unordered_map<std::string, int>& counts) {
    if (ShouldSkip(node))
        return;

    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        size_t len;
        lxb_char_t* text = lxb_dom_node_text_content(node, &len);
        if (text && len > 0) {
            ParseText(std::string(reinterpret_cast<char*>(text), len), counts);
        }
    }

    for (lxb_dom_node_t* child = node->first_child; child != nullptr; child = child->next)
        Traverse(child, counts);
}

void AddToDB(pqxx::connection& cx, const std::string& url, std::unordered_map<std::string, int> counts) {
    // start a transaction
    pqxx::work tx{cx};

    int urlId = tx.query_value<int>(pqxx::prepped("insertUrl"), pqxx::params(url));

    for (auto& [word, count] : counts) {
        int wordId = tx.query_value<int>(pqxx::prepped("insertWord"), pqxx::params(word));
        // std::cout << "adding \"" << word << "\" (" << wordId << "), url: \"" << url << " (" << urlId << "), count: " << count << "\n";
        tx.exec(pqxx::prepped("insertInvertedIndex"), pqxx::params(wordId, urlId, count));
    }

    // insert url, if not already, into db and get id
    // insert word, if not already, into db and get id
    // insert connection into list, with count

    // Commit the transaction
    tx.commit();
}

namespace Indexer {
void ExtractKeywords(pqxx::connection& cx, const std::string& url, lxb_html_document_t* document, lxb_dom_collection_t *collection) {
    std::unordered_map<std::string, int> counts;
    lxb_dom_node_t* root = lxb_dom_interface_node(document);
    Traverse(root, counts);

    AddToDB(cx, url, counts);

    /*
    std::cout << "keywords\n";

    lxb_dom_element_t *element;
    lxb_dom_node_t* node;
    lxb_char_t* text;

    lxb_status_t status = lxb_html_document_parse(document, reinterpret_cast<const unsigned char*>(html.c_str()), html.size());
    if (status != LXB_STATUS_OK)
        printf("Something went wrong 2.\n");

    std::vector<std::string> tags = { "p", "h1", "h2", "h3", "h4", "h5", "h6", "title" };
    for (std::string& tag : tags) {
        status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->body), collection, reinterpret_cast<const unsigned char*>(tag.c_str()), tag.size());
        if (status != LXB_STATUS_OK)
            printf("status is not OK 1.\n");

        for (size_t i = 0; i < lxb_dom_collection_length(collection); i++) {
            element = lxb_dom_collection_element(collection, i);

            node = lxb_dom_interface_node(element);
            size_t len;
            text = lxb_dom_node_text_content(node, &len);
            if (text) {
                std::cout << tag << ": " << std::string(reinterpret_cast<char*>(text), len) << "\n";
            }
        }
    }

    // Cleanup
    lxb_dom_collection_clean(collection);
    lxb_html_document_clean(document);
    */
}

void SetupDB(pqxx::connection& cx) {
    cx.prepare(
        "insertWord",
        "INSERT INTO words (word) VALUES ($1) "
        "ON CONFLICT (word) "
        "DO UPDATE SET word = EXCLUDED.word "
        "RETURNING id"
    );

    cx.prepare(
        "insertUrl",
        "INSERT INTO urls (address) VALUES ($1) "
        "ON CONFLICT (address) "
        "DO UPDATE SET address = EXCLUDED.address "
        "RETURNING id"
    );

    cx.prepare(
        "insertInvertedIndex",
        "INSERT INTO inverted_index (wordId, urlId, count) VALUES ($1, $2, $3) "
        "ON CONFLICT (wordId, urlId) DO NOTHING"
    );
}

void Init(pqxx::connection& cx) {
    SetupDB(cx);

    llama_backend_init();

    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 99;

    model = llama_model_load_from_file("models/nomic-embed-text-v1.5.f32.gguf", model_params);

    if (!model) {
        std::cout << "Failed loading model\n";
        return;
    }

    llama_context_params ctx_params = llama_context_default_params();

    ctx_params.embeddings = true;
    ctx_params.n_ctx = 512;

    ctx = llama_init_from_model(model, ctx_params);

    // Initialize Stop Words Set
    std::ifstream stopWordsFile("crawler/stopWords.txt");
    if (stopWordsFile.is_open()) {
        std::string line;
        while(std::getline(stopWordsFile, line)) {
            stopWords.insert(line);
        }
    }
}

void CleanUp() {
    llama_free(ctx);
    llama_model_free(model);

    llama_backend_free();
}

float CalcCosineSimilarity(const std::vector<float>& emb1, const std::vector<float>& emb2) {
    // (A . B) / (||A|| * ||B||)

    // calculate sum and dot product
    double sum1 = 0, sum2 = 0;
    double dotProduct = 0;
    for (size_t i = 0; i < emb1.size(); ++i) {
        sum1 += emb1[i] * emb1[i];
        sum2 += emb2[i] * emb2[i];
        dotProduct += emb1[i] * emb2[i];
    }

    return dotProduct / (std::sqrt(sum1) * std::sqrt(sum2));
}

float GetSimilarity(const std::string& input1, const std::string& input2) {
    std::vector<float> embedding = embed_text(model, ctx, "search_query: " + input1);
    std::vector<float> embedding2 = embed_text(model, ctx, "search_query: " + input2);
    return CalcCosineSimilarity(embedding, embedding2);
}
}
