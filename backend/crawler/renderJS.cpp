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
#include <sys/prctl.h>
#include <openssl/sha.h>

std::string debuggerUrl = "";
pid_t pid = -1;
CURL* curl = nullptr;
CURL* imageCurl = nullptr;
int timeoutTime = 10;
int totalTimeoutTime = 30;

ix::WebSocket webSocket;

bool gettingHTML = false;
std::string htmlBody = "";

bool finishedSetup = false;

namespace Renderer {
static size_t write_data(char *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

size_t write_byte_data(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* buffer = static_cast<std::vector<unsigned char>*>(userp);
    
    unsigned char* bytes = static_cast<unsigned char*>(contents);
    buffer->insert(buffer->end(), bytes, bytes + total);

    return total;
}

void EnablePage(ix::WebSocket& webSocket) {
    std::cout << "enable page\n";
    // enable page
    nlohmann::json pageDomain;
    pageDomain["id"] = 1;
    pageDomain["method"] = "Page.enable";
    webSocket.send(pageDomain.dump());
}

void FinishedSetup() {
    // something here to tell main that we are good to go
    std::cout << "finished setup\n";
    finishedSetup = true;
}

void NavigatePage(ix::WebSocket& webSocket, const std::string& url) {
    std::cout << "navigate page\n";
    nlohmann::json navigate;
    navigate["id"] = 2;
    navigate["method"] = "Page.navigate";
    navigate["params"] = { { "url", url } };
    webSocket.send(navigate.dump());
}

void EvaluateHTML(ix::WebSocket& webSocket) {
    std::cout << "evanluateHTML\n";
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
    htmlBody = json["result"]["result"]["value"].get<std::string>();
    gettingHTML = false;
}

// returns the rendered html
std::string GetHTML(const std::string& url, long* httpCode) {
    gettingHTML = true;
    NavigatePage(webSocket, url);
    if (httpCode)
        *httpCode = 200; // temp

    std::cout << "navigated, now gonig to wait\n";
    while (gettingHTML) // wait until received rendered HTML body
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    return htmlBody;
}

void StartClient(const std::string& debuggerUrl) {
    // connect to a server
    webSocket.setUrl(debuggerUrl);

    std::cout << "Connecting to " << debuggerUrl << "..." << std::endl;

    // setup a callback to be fired
    webSocket.setOnMessageCallback([](const ix::WebSocketMessagePtr& msg) {
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
}

void Init() {
    curl = curl_easy_init();

    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeoutTime);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, totalTimeoutTime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);

    imageCurl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeoutTime);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, totalTimeoutTime);
    curl_easy_setopt(imageCurl, CURLOPT_WRITEFUNCTION, write_byte_data);
    curl_easy_setopt(imageCurl, CURLOPT_FOLLOWLOCATION, 1L);

    pid = fork();

    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM); // kill child task if parent task stops
        execlp("chromium",
            "chromium",
            "--headless=new",
            "--remote-debugging-port=9222",
            "--remote-allow-origins=ws://127.0.0.1:9222",
            nullptr
        );

        _exit(1);
    }

    LaunchChromium();
}

std::string CurlGet(const std::string& url, long* httpCode) {
    std::string html = "";
    CURLcode res;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);

    res = curl_easy_perform(curl); // perform request
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Transfer failed: %s\n", curl_easy_strerror(res));
        throw std::runtime_error("Failed!, res is not ok\n");
    }

    // extract the server's HTTP response code
    if (httpCode) {
        *httpCode = 0; // init to 0
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, httpCode);
        printf("HTTP Status Code: %ld, for: %s\n\n", *httpCode, url.c_str());
    }

    return html;
}

std::vector<unsigned char> DownloadImage(const std::string& url) {
    std::vector<unsigned char> data;

    curl_easy_setopt(imageCurl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(imageCurl, CURLOPT_WRITEDATA, &data);


    CURLcode result = curl_easy_perform(imageCurl);

    if (result != CURLE_OK)
        data.clear();

    return data;
}

void LaunchChromium() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // temp

    std::string data = CurlGet("127.0.0.1:9222/json/list", nullptr);

    nlohmann::json json = nlohmann::json::parse(data);
    StartClient(json[2]["webSocketDebuggerUrl"]);

    while (!finishedSetup)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void CleanUp() {
    curl_easy_cleanup(curl);
    curl_easy_cleanup(imageCurl);
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
}

std::string Hash(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];

    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash);

    std::stringstream ss;
    for (unsigned char c : hash) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }

    return ss.str();
}
} // namespace Renderer
