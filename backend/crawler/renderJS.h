#pragma once

#include <string>
#include <vector>
#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>

namespace Renderer {
    void Init();
    void StartClient(const std::string& debuggerUrl);

    void LaunchChromium();
    std::string CurlGet(const std::string& url, long* httpCode);
    std::vector<unsigned char> DownloadImage(const std::string& url);
    std::string GetHTML(lxb_html_document_t* document, const std::string& url, long* httpCode);

    void CleanUp();

    std::string Hash(const std::string& input);
}
