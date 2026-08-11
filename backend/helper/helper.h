#pragma once

#include <vector>
#include <string>

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

} // Helper
