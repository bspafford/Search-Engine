#include "helper.h"

#include <iostream>
#include <algorithm>
#include <fstream>
#include <unordered_set>
#include <chrono>

std::unordered_set<std::string> stopWords;

std::chrono::steady_clock::time_point durationTime;

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
} // Helper
