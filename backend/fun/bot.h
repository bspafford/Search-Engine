#pragma once

#include <string>

namespace Bot {
    void Init();
    void CleanUp();
    double GetSimilarity(const std::string& input1, const std::string& input2);
}
