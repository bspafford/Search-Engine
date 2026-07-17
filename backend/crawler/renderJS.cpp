#include "renderJS.h"

#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstdlib>
#include <curl/curl.h>
#include <chrono>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXUserAgent.h>
#include <nlohmann/json.hpp>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static size_t write_data1(char *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

void EnablePage(ix::WebSocket& webSocket) {
    // enable page
    nlohmann::json pageDomain;
    pageDomain["id"] = 1;
    pageDomain["method"] = "Page.enable";
    webSocket.send(pageDomain.dump());
}

void FinishedSetup() {
    // something here to tell main that we are good to go
}

void NavigatePage(ix::WebSocket& webSocket, const std::string& url) {
    nlohmann::json navigate;
    navigate["id"] = 2;
    navigate["method"] = "Page.navigate";
    navigate["params"] = { { "url", url } };
    webSocket.send(navigate.dump());
}

void EvaluateHTML(ix::WebSocket& webSocket) {
    nlohmann::json navigate;
    navigate["id"] = 3;
    navigate["method"] = "Runtime.evaluate";
    navigate["params"] = {
        { "expression", "document.documentElement.outerHTML" },
        { "returnByValue", true }
    };
    webSocket.send(navigate.dump());
}

void RenderedHTML(const nlohmann::json& json) {
    std::cout << "The HTML CODE:\n" << json["result"]["result"]["value"].get<std::string>() << "\n\n";
}

void Client(const std::string& url, const std::string& debuggerUrl) {
    ix::initNetSystem();

    ix::WebSocket webSocket;

    // connect to a server
    webSocket.setUrl(debuggerUrl);

    std::cout << "Connecting to " << debuggerUrl << "..." << std::endl;

    // setup a callback to be fired
    webSocket.setOnMessageCallback([&webSocket, url](const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Message) {
            nlohmann::json json = nlohmann::json::parse(msg->str);
            if (!json.contains("id")) {
                if (json["method"] == "Page.loadEventFired")
                    EvaluateHTML(webSocket);

                return;
            }

            int id = json["id"];
            if (id == 1) {
                FinishedSetup();
            } else if (id == 3) {
                RenderedHTML(json);
            }
        }
        else if (msg->type == ix::WebSocketMessageType::Open) {
            std::cout << "Connection established" << std::endl;
            EnablePage(webSocket);
        }
        else if (msg->type == ix::WebSocketMessageType::Error) {
            // Maybe SSL is not configured properly
            std::cout << "Connection error: " << msg->errorInfo.reason << std::endl;
        }
    });

    webSocket.start();

    std::string text;
    while (std::getline(std::cin, text)) {
        webSocket.send(text);
        std::cout << "> " << std::flush;
    }
}

CURL* Init() {
    CURL* curl = curl_easy_init();

    int timeoutTime = 10;
    int totalTimeoutTime = 30;

    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeoutTime);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, totalTimeoutTime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data1);
    // curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

    if (!curl)
        throw std::runtime_error("Curl was null\n");

    return curl;
}

std::string CurlGet(CURL* curl, const std::string& url) {
    std::string html = "";
    CURLcode res;
    long httpCode = -1;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);

    res = curl_easy_perform(curl); // perform request
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Transfer failed: %s\n", curl_easy_strerror(res));
        throw std::runtime_error("Failed!, res is not ok\n");
    }

    // extract the server's HTTP response code
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    printf("HTTP Status Code: %ld, for: %s\n\n", httpCode, url.c_str());

    return html;
}

std::string LaunchChromium(CURL* curl) {
    /*
    auto start = std::chrono::steady_clock::now();

    while (true) {
        if (!CurlGet(curl, "http://127.0.0.1:9222/json/version").empty()) {
            break; // Chromium is ready
        }

        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) {
            throw std::runtime_error("Chromium failed to start");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    */

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    std::string data = CurlGet(curl, "127.0.0.1:9222/json/list");

    nlohmann::json json = nlohmann::json::parse(data);
    return json[2]["webSocketDebuggerUrl"];
}

std::string RenderPage(const std::string& url) {
    CURL* curl = Init();

    pid_t pid = fork();

    if (pid == 0) {
        execlp("chromium",
            "chromium",
            "--headless=new",
            "--remote-debugging-port=9222",
            "--remote-allow-origins=ws://127.0.0.1:9222",
            nullptr
        );

        _exit(1);
    }

    std::string debuggerUrl = LaunchChromium(curl);

    Client(url, debuggerUrl);

    curl_easy_cleanup(curl);

    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
    return "";
}
