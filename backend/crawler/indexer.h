#pragma once

#include <string>

namespace Indexer {
    void Init();
    void CleanUp();
    float GetSimilarity(const std::string& input1, const std::string& input2);
}
