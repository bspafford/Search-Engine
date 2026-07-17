#pragma once

#include <string>
#include <curl/curl.h>

CURL* Init();
std::string RenderPage(const std::string& url);

std::string LaunchChromium(CURL* curl);
std::string CurlGet(CURL* curl, const std::string& url);
