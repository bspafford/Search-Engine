#pragma once

#include <string>
#include <vector>

namespace Renderer {
    void Init();
    void StartClient(const std::string& debuggerUrl);

    void LaunchChromium();
    std::string CurlGet(const std::string& url, long* httpCode);
    std::vector<unsigned char> DownloadImage(const std::string& url);
    std::string GetHTML(const std::string& url, long* httpCode);

    void CleanUp();

    std::string Hash(const std::string& input);
}
