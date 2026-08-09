#pragma once

#include <string>
#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXUserAgent.h>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

class ThreadPool;

class Renderer {
public:
    ~Renderer();

    static void InitChromium();
    static void InitPool(int threads);
    void Init();
    void StartClient(const std::string& debuggerUrl);

    void LaunchChromium();
    std::string CurlGet(const std::string& url, long* httpCode, const std::string& method);
    std::string GetHTML(const std::string& url, long* httpCode);

    static void Render(const std::string& url);

    void CleanUp();

    static Renderer& GetRenderer() {
        thread_local Renderer renderer;
        thread_local bool initialized = false;
        if (!initialized) {
            renderer.Init();
            initialized = true;
        }
        return renderer;
    }

private:
    void EnablePage(ix::WebSocket& webSocket);
    void FinishedSetup();
    void NavigatePage(ix::WebSocket& webSocket, const std::string& url);
    void EvaluateHTML(ix::WebSocket& webSocket);
    void RenderedHTML(const nlohmann::json& json);
    void WaitFor(bool& con, const std::string& debugStr);
    bool HasTitle();
    bool HasDescription();
    bool HasFavicon();
    bool HasEnoughContent(const std::string& html);

    lxb_html_document_t* document = nullptr;

    std::string debuggerUrl = "";
    CURL* curl = nullptr;
    int timeoutTime = 10;
    int totalTimeoutTime = 30;

    ix::WebSocket webSocket;

    bool gettingHTML = false;
    std::string htmlBody = "";

    bool finishedSetup = false;

    static inline pid_t pid = -1;

    static inline ThreadPool* threadPool = nullptr;
};
