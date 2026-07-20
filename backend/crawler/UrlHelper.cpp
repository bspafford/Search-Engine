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

    // add subdomain, example: "https://google.com/" --> "https://www.google.com/"
    NormalizeSubdomain(url);

    // HTTP vs HTTPS
        // check if http redirects to https, if does, then dont need http (most of the time it will)
    // default ports
        // if port is :80 with protocol http://, then thats default
        // same with :443 on https://
        // but if :8124 then should keep port
    
}

// add subdomain, example: "https://example.com/" --> "https://www.example.com/"
// "https://blog.example.com/" --> same
void NormalizeSubdomain(std::string& url) {
    // remove protocol and path
    size_t protocolPos = url.find("://");
    size_t slashPos = url.find('/', protocolPos + 3); // find first '/' after protocol
    std::string pathStr = url.substr(slashPos); // save everything after '/'
    std::string protocolStr = url.substr(0, protocolPos + 3);
    url.erase(slashPos); // remove everything after '/'
    url.erase(0, protocolPos + 3);

    // find public suffix
    // find longest public suffix in url
    std::string longestSuffix = "";
    PSLRule rule;
    // catch case, incase have a rule like !www.ck, and url = www.ck, would have to parse to beginning
    auto it = pslMap.find(url);
    if (it != pslMap.end()) {
        longestSuffix = url;
        rule = it->second;
    } else {
        size_t pos = url.rfind('.');
        while (pos != std::string::npos) {
            std::string suffix = url.substr(pos + 1); // dont include '.'
            it = pslMap.find(suffix);
            if (it != pslMap.end()) { // found suffix in PSL
                longestSuffix = suffix;
                rule = it->second;
            }

            pos = url.rfind('.', pos - 1); // increase suffix to see if still valid
        }
    }

    if (longestSuffix.empty())
        throw std::runtime_error("Suffix is empty on url: \"" + url + "\"\n");

    // remove suffix
    // wait until get down here
    // cache the iterator. then check if either a wildcard or exception
    // if exception then the first word before '.' is the domain, everthing after is the suffix
    // if wildcard, then remove the last word, the remaining is the domain
    if (rule.exception) {
        size_t exceptionPos = longestSuffix.find('.');
        // https://asd.www.ck
        // rule: !www.ck
        size_t fromBack = longestSuffix.size() - exceptionPos;
        url = url.substr(0, url.size() - fromBack);
        longestSuffix = longestSuffix.substr(exceptionPos + 1);
    } else if (rule.wildcard) {
        // remove longest suffix
        // then additionally the next word until '.' from the back
        url = url.substr(0, url.size() - longestSuffix.size() - 1);
        size_t wildcardPos = url.rfind('.');
        longestSuffix = url.substr(wildcardPos + 1) + "." + longestSuffix; // update suffix to include wildcard word
        url = url.substr(0, wildcardPos);
    } else {
        url = url.substr(0, url.size() - longestSuffix.size() - 1);
    }

    size_t subdomainPos = url.rfind('.');
    if (subdomainPos == std::string::npos)
        url = "www." + url;

    url = protocolStr + url + "." + longestSuffix + pathStr;
}

} // UrlHelper
