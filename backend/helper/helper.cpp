#include "helper.h"

#include <algorithm>
#include <fstream>
#include <unordered_set>

std::unordered_set<std::string> stopWords;

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
void ParseText(std::string text, std::unordered_map<std::string, int>& counts) {
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
            ++counts[std::string(word)];
    }
}
} // Helper
