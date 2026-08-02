#include "renderJS.h"
#include "helper.h"

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
#include <lexbor/core/base.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/interface.h>
#include <lexbor/html/html.h>
#include <lexbor/url/url.h>

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

// wait for things like title, favicon, description, open graph tags?
// stop the page downloading/stop the window, but dont close window to reopen, just see if i can stop or goto new
// maybe check to see if there is enough html first. If there is then skip rendering all together, otherwise render

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

void WaitFor(bool& con, const std::string& debugStr) {
    int sleepTime = 0;
    while (con) { // wait until received rendered HTML body
        if (sleepTime >= timeoutTime * 1000) {
            std::cout << "Took too long, timing out: " << debugStr << "\n";
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ++sleepTime;
    }
}

bool HasTitle(lxb_html_document_t* document) {
    // get title
    lxb_dom_collection_t* title = lxb_dom_collection_make(&document->dom_document, 1);
    if (!title) {
        lxb_dom_collection_destroy(title, true);
        return false;
    }

    lxb_status_t status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->head), title, (const lxb_char_t*)"title", 5);
    lxb_dom_collection_destroy(title, true);
    return status == LXB_STATUS_OK;
}

bool HasDescription(lxb_html_document_t* document) {
    lxb_dom_element_t* element;
    const lxb_char_t* name;
    size_t len;

    lxb_dom_collection_t* description = lxb_dom_collection_make(&document->dom_document, 32);
    if (!description) {
        printf("description is null");
        return false;
    }

    lxb_status_t status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->head), description, (const lxb_char_t*)"meta", 4);
    if (status != LXB_STATUS_OK) {
        lxb_dom_collection_destroy(description, true);
        printf("No description found");
        return false;
    }

    for (int i = 0; i < lxb_dom_collection_length(description); ++i) {
        element = lxb_dom_collection_element(description, i);
        name = lxb_dom_element_get_attribute(element, (const lxb_char_t*) "name", 4, &len);
        if (!name) // was no name attribute
            continue;

        if (std::strcmp(reinterpret_cast<const char*>(name), "description") != 0)
            continue;

        lxb_dom_collection_destroy(description, true);
        return true;
    }

    // Cleanup.
    lxb_dom_collection_destroy(description, true);
    return false;
}

bool HasFavicon(lxb_html_document_t* document) {
    lxb_dom_element_t* element;
    const lxb_char_t* rel;
    size_t len;

    lxb_dom_collection_t* favicon = lxb_dom_collection_make(&document->dom_document, 32);
    if (!favicon) {
        printf("favicon is null");
        return false;
    }

    lxb_status_t status = lxb_dom_elements_by_tag_name(lxb_dom_interface_element(document->head), favicon, (const lxb_char_t*)"link", 4);
    if (status != LXB_STATUS_OK) {
        lxb_dom_collection_destroy(favicon, true);
        printf("No favicon found");
        return false;
    }

    std::cout << "favicon length: " << lxb_dom_collection_length(favicon) << "\n";
    for (int i = 0; i < lxb_dom_collection_length(favicon); ++i) {
        element = lxb_dom_collection_element(favicon, i);
        rel = lxb_dom_element_get_attribute(element, (const lxb_char_t*) "rel", 3, &len);

        if (!rel) // was no name attribute
            continue;

        if (std::strcmp(reinterpret_cast<const char*>(rel), "icon") != 0)
            continue;

        lxb_dom_collection_destroy(favicon, true);
        return true;
    }

    /* Cleanup. */
    lxb_dom_collection_destroy(favicon, true);
    std::cout << "No favicon found 1\n";
    return false;
}

// Determines if there is enough content in the HTML or if it should be rendered
// Checks things like title, favicon, description, etc
bool HasEnoughContent(lxb_html_document_t* document, const std::string& html) {
    lxb_status_t status;

    // Parse the HTML document
    status = lxb_html_document_parse(document, reinterpret_cast<const lxb_char_t*>(html.c_str()), html.size());
    if (status != LXB_STATUS_OK)
        printf("Something went wrong 2.\n");

    bool title = HasTitle(document);
    bool description = HasDescription(document);
    bool favicon = HasFavicon(document);

    // Cleanup
    lxb_html_document_clean(document);

    std::cout << "\033[34mtitle: " << title << ", description: " << description << ", favicon: " << favicon << "\n\033[0m";
    return title && description && favicon;
}

// returns the rendered html
std::string GetHTML(lxb_html_document_t* document, const std::string& url, long* httpCode) {
    Helper::StartTimer("Getting HTML");

    std::string html = CurlGet(url, httpCode);
    if (HasEnoughContent(document, html)) {
        std::cout << "\033[34mWas enough, didn't need to render\n\033[0m";
        return html;
    }

    gettingHTML = true;
    NavigatePage(webSocket, url);
    if (httpCode)
        *httpCode = 200; // temp

    std::cout << "navigated, now gonig to wait\n";
    WaitFor(gettingHTML, "Navigating");

    Helper::EndTimer("Finished Getting HTML");

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
            "--disable-gpu",
            "--disable-extensions",
            "--disable-background-networking",
            "--disable-background-timer-throttling",
            "--disable-backgrounding-occluded-windows",
            "--disable-breakpad",
            "--disable-component-update",
            "--disable-default-apps",
            "--disable-dev-shm-usage",
            "--disable-features=Translate,BackForwardCache,AcceptCHFrame,MediaRouter",
            "--disable-hang-monitor",
            "--disable-ipc-flooding-protection",
            "--disable-popup-blocking",
            "--disable-prompt-on-repost",
            "--disable-renderer-backgrounding",
            "--disable-sync",
            "--disable-notifications",
            "--disable-client-side-phishing-detection",
            "--disable-domain-reliability",
            "--disable-features=OptimizationHints,InterestFeedContentSuggestions",
            "--metrics-recording-only",
            "--mute-audio",
            "--no-first-run",
            "--no-default-browser-check",
            "--password-store=basic",
            "--use-mock-keychain",
            "--hide-scrollbars",
            "--blink-settings=imagesEnabled=false",
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

    std::cout << "data: " << data << "\n";
    nlohmann::json json = nlohmann::json::parse(data);
    int idx = 0;
    for (int i = 0; i < json.size(); ++i) {
        if (json[i]["type"] == "page") {
            idx = i;
            break;
        }
    }

    StartClient(json[idx]["webSocketDebuggerUrl"]);

    WaitFor(finishedSetup, "Setup");
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
