#pragma once

#include <string>
#include <vector>

namespace Bot {
    void Init(std::vector<float>& randWordEmbd, const std::string& randWord);
    void CleanUp();
    double GetSimilarity(const std::string& input1, const std::vector<float>& embd);
}
