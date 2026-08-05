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

    // returns base url
    // e.g. https://www.google.com/robots.txt would return "https://www.google.com" AND path == "/"
    std::string ExtractOrigin(const std::string& url, std::string* path);
}
