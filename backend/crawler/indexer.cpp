#include "llama.h"

#include <iostream>
#include <vector>
#include <string>
#include <cmath>

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

namespace Indexer {
void Init() {
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
