#include "UrlHelper.h"

#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <fstream>

std::unordered_map<std::string, PSLRule> pslMap;

namespace UrlHelper {
void Init() {
    std::ifstream pslFile("crawler/psl.txt");
    if (!pslFile.is_open()) {
        std::cerr << "Error: Could not open file\n";
        return;
    }

    std::string line;
    while (std::getline(pslFile, line)) {
        if (line.empty() || line.starts_with("//"))
            continue;

        bool wildCard = line.starts_with("*");
        bool exception = line.starts_with("!");
        if (wildCard)
            line.erase(0, 2); // remove '*.'
        else if (exception)
            line.erase(0, 1); // remove '!'

        pslMap.insert({ line, { wildCard, exception } });
    }

    pslFile.close();
}

void Normalize(std::string& url) {
    // removes ?...
    std::size_t pos = url.find('?', 0);
    if (pos != std::string::npos) // GET was in string
        url = url.substr(0, pos);

    // removes #...
    pos = url.find('#', 0);
    if (pos != std::string::npos)
        url = url.substr(0, pos);

    // if it doesn't have a trailing slash, it will add one
    url += url.back() != '/' ? "/" : "";

    // make all lowercase
    std::transform(url.begin(), url.end(), url.begin(), [](unsigned char c) { return std::tolower(c); });

    // HTTP vs HTTPS
        // check if http redirects to https, if does, then dont need http (most of the time it will)
    // default ports
        // if port is :80 with protocol http://, then thats default
        // same with :443 on https://
        // but if :8124 then should keep port
}

std::string ExtractOrigin(const std::string& url, std::string* path) {
    // Find "://"
    std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        throw std::runtime_error("Invalid URL (ExtractOrigin): " + url);

    // Find first '/' after the host
    std::size_t pathStart = url.find('/', schemeEnd + 3);

    // make sure to have trailing '/' on URLs
    if (pathStart == std::string::npos) {
        if (path) *path = "/";
        bool addTrailing = url.back() != '/';
        return url + std::string(addTrailing ? "/" : "");
    } else {
        if (path) *path = url.substr(pathStart);
        return url.substr(0, pathStart) + "/";
    }
}
} // UrlHelper
