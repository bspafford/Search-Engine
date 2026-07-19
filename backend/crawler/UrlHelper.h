#pragma once

#include <string>

struct PSLRule {
    bool wildcard;
    bool exception;
};

namespace UrlHelper {
    void Init();

    // removes GET tags (everything after '?' OR '#')
    // Takes in url by reference
    void Normalize(std::string& url);
    void NormalizeSubdomain(std::string& url);
}
