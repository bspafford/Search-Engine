#include "Model.h"
#include "Database.h"

#include <iostream>
#include <nlohmann/json.hpp>

void Model::Init() {
	llama_backend_init();
	llama_log_set(SilentLog, nullptr);

	llama_model_params model_params = llama_model_default_params();
	model_params.n_gpu_layers = -1;

	std::string modelName = "nomic-embed-text-v1.5.f32.gguf";
    std::string modelPath = "../Models/" + modelName;
	std::cout << "Using Embedding Model: " << modelName << "\n";

	model = llama_model_load_from_file(modelPath.c_str(), model_params);

	if (!model) {
		throw std::runtime_error("Failed loading model\n");
		return;
	}

	llama_context_params ctx_params = llama_context_default_params();

	ctx_params.embeddings = true;
    const int n_ctx_train = llama_model_n_ctx_train(model);
	ctx_params.n_ctx = n_ctx_train;
    ctx_params.n_ubatch = n_ctx_train;
    // max batch size
    n_batch = n_ctx_train;

	ctx = llama_init_from_model(model, ctx_params);
	vocab = llama_model_get_vocab(model);
}

void Model::EmbedText(long id, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex);

    auto tokens = common_tokenize(vocab, text, true, true);

    llama_batch batch = llama_batch_init(n_batch, 0, 1);

    if (tokens.size() > n_batch) {
        std::cerr << "Too many tokens: " << tokens.size() << " > n_batch: " << n_batch << '\n';
        return;
    }

    if (tokens.size() > llama_n_ctx(ctx)) {
        std::cerr << "Too many tokens for context: " << tokens.size() << " > n_ctx: " << llama_n_ctx(ctx) << '\n';
        return;
    }

    // clamp the max size of tokens to n_batch
    if (tokens.size() > n_batch)
        tokens.resize(n_batch);

    for (size_t i = 0; i < tokens.size(); i++) {
        batch.token[batch.n_tokens] = tokens[i];
        batch.pos[batch.n_tokens] = i;
        batch.n_seq_id[batch.n_tokens] = 1;
        batch.seq_id[batch.n_tokens][0] = 0;
        batch.logits[batch.n_tokens] = false;// (i == tokens.size() - 1);
        batch.n_tokens++;
    }

    // Clear KV cache (not needed for embeddings)
    llama_memory_clear(llama_get_memory(ctx), true);

    //printf("text: %s, tokens: %ld, n_batch: %lld\n", text.c_str(), tokens.size(), n_batch);
    if (llama_decode(ctx, batch))
        throw std::runtime_error("Embedding failed.");

    const float* emb = llama_get_embeddings_seq(ctx, 0);

    int dim = llama_model_n_embd_out(model);

    std::vector<float> result(emb, emb + dim);

    Normalize(result);

    llama_batch_free(batch);

    // upload 768 to database
    Database::UploadEmbeddings(id, result);
}

// Simple L2 normalization
void Model::Normalize(std::vector<float>& v) {
    float sum = 0.0f;

    for (float x : v)
        sum += x * x;

    float inv = 1.0f / std::sqrt(sum);

    for (float& x : v)
        x *= inv;
}

// Tokenize
std::vector<llama_token> Model::Tokenize(const llama_vocab* vocab, const std::string& text) {
    int count = llama_tokenize(vocab, text.c_str(), text.size(), nullptr, 0, true, true);
    std::cout << "count: " << count << "\n";

    std::vector<llama_token> tokens(-count);

    llama_tokenize(vocab, text.c_str(), text.size(), tokens.data(), tokens.size(), true, true);

    return tokens;
}


std::vector<llama_token> Model::common_tokenize(const struct llama_vocab* vocab, const std::string& text, bool add_special, bool parse_special) {
    // upper limit for the number of tokens
    int n_tokens = text.length() + 2 * add_special;
    std::vector<llama_token> result(n_tokens);
    n_tokens = llama_tokenize(vocab, text.data(), text.length(), result.data(), result.size(), add_special, parse_special);
    //if (n_tokens == std::numeric_limits<int32_t>::min()) {
        //throw std::runtime_error("Tokenization failed: input text too large, tokenization result exceeds int32_t limit");
    //}
    if (n_tokens < 0) {
        result.resize(-n_tokens);
        int check = llama_tokenize(vocab, text.data(), text.length(), result.data(), result.size(), add_special, parse_special);
        GGML_ASSERT(check == -n_tokens);
    } else {
        result.resize(n_tokens);
    }
    return result;
}