#include "helper.h"

#include <iostream>
#include <algorithm>
#include <fstream>
#include <unordered_set>
#include <chrono>
#include <cmath>

std::unordered_set<std::string> stopWords;

std::chrono::steady_clock::time_point durationTime;

void silentLog(ggml_log_level level, const char* text, void* user_data) {
    // do nothing
}

void SetupStopWords() {
    if (!stopWords.empty())
        return;

    // Initialize Stop Words Set
    std::ifstream stopWordsFile("crawler/stopWords.txt");
    if (stopWordsFile.is_open()) {
        std::string line;
        while(std::getline(stopWordsFile, line)) {
            stopWords.insert(line);
        }
    }
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

namespace Helper {
void ParseText(std::string text, std::vector<WordData>& words, const std::string& tagName) {
    SetupStopWords();

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
            words.push_back({ std::string(word), tagName });
    }
}

void StartTimer(const std::string& debugStr) {
    durationTime = std::chrono::steady_clock::now();

    std::cout << "\033[32mStarted: " << debugStr << "\033[0m\n";
}

void EndTimer(const std::string& debugStr) {
    auto elapsed = duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - durationTime);

    auto total = elapsed.count();

    int h  = total / 3'600'000;
    int m  = (total % 3'600'000) / 60'000;
    int s  = (total % 60'000) / 1'000;
    int ms = total % 1'000;

    std::cout << "\033[32m" << debugStr << " | " << h << "h " << m << "m " << s << "s " << ms << "ms\033[0m\n";
}

void InitEmbedModel(llama_model*& model, llama_context*& ctx, const llama_vocab*& vocab, const std::string& modelName) {
    printf("init\n");
    llama_backend_init();
    llama_log_set(silentLog, nullptr);

    printf("params\n");
    llama_model_params model_params = llama_model_default_params();
    printf("end\n");
    model_params.n_gpu_layers = -1;

    std::cout << "Using Embedding Model: \"" << modelName << "\"\n";
    model = llama_model_load_from_file(("models/" + modelName).c_str(), model_params);

    if (!model) {
        std::cout << "Failed loading model\n";
        return;
    }

    llama_context_params ctx_params = llama_context_default_params();

    ctx_params.embeddings = true;
    ctx_params.n_ctx = 512;

    ctx = llama_init_from_model(model, ctx_params);
    vocab = llama_model_get_vocab(model);
}
//
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

std::vector<float> EmbedText(llama_model* model, llama_context* ctx, const llama_vocab* vocab, const std::string& text) {

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

} // Helper
