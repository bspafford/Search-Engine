#pragma once

#include <vector>
#include <string>
#include <llama.h>

struct WordData{
    std::string word = "";
    std::string field = "";

    bool operator==(const WordData& other) const {
        return word == other.word &&
               field == other.field;
    }
};

struct WordDataHash {
    size_t operator()(const WordData& p) const {
        size_t h1 = std::hash<std::string>{}(p.word);
        size_t h2 = std::hash<std::string>{}(p.field);

        return h1 ^ (h2 << 1);
    }
};

namespace Helper {

void ParseText(std::string text, std::vector<WordData>& words, const std::string& tagName);
void StartTimer(const std::string& debugStr = "");
void EndTimer(const std::string& debugStr = "");

void InitEmbedModel(llama_model*& model, llama_context*& ctx, const llama_vocab*& vocab, const std::string& modelString = "nomic-embed-text-v1.5.f32.gguf");
static void normalize(std::vector<float>& v);
static std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text);
std::vector<float> EmbedText(llama_model* model, llama_context* ctx, const llama_vocab* vocab, const std::string& text);

} // Helper
