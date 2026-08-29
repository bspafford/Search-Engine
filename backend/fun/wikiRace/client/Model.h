#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <llama.h>

class Model {
public:
	static void Init();
	static void EmbedText(long id, const std::string& text);

private:
	static void Normalize(std::vector<float>& v);
	static std::vector<llama_token> common_tokenize(const struct llama_vocab* vocab, const std::string& text, bool add_special, bool parse_special);
	static std::vector<llama_token> Tokenize(const llama_vocab* vocab, const std::string& text);

	static void SilentLog(ggml_log_level level, const char* txt, void* user_data) {}

	static inline std::mutex mutex;

	static inline llama_model* model;
	static inline llama_context* ctx;
	static inline const llama_vocab* vocab;

	static inline uint64_t n_batch;
};