#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <App.h>
#include <sstream>
#include <ctemplate/template.h>
#include <ctemplate/template.h>
#include <ctemplate/template_dictionary.h>
#include <ctemplate/template_enums.h>
#include <fstream>

#include "bot.h"

std::string randWord = "";
std::vector<float> randWordEmbd;

int randInt(int lo, int hi) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> distrib(lo, hi);
    return distrib(gen);
}

void OutputGuesses(std::vector<std::pair<double, std::string>>& guesses) {
    // sort
    std::sort(guesses.begin(), guesses.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    for (int i = 0; i < guesses.size(); ++i) {
        std::cout << "#" << (i + 1) << ": " << guesses[i].second << ", " << int(guesses[i].first) << "\n";
    }
}

void Init() {
    std::vector<std::string> randWordList = { "cat", "dog", "car", "plane", "house", "computer", "cloud" };

    randWord = "house";//randWordList[randInt(0, randWordList.size() - 1)];
    std::cout << "Guess the word! (hint: " << randWord << ")\n";

    Bot::Init(randWordEmbd, randWord);
}

bool Guess(const std::string& guess, double& score) {
    double diff = Bot::GetSimilarity(guess, randWordEmbd);
    if (guess == randWord) {
        std::cout << "\nYou got it!\n\n";
        score = 1.0;
        return true;
    } else {
        score = std::clamp(sqrt(diff), 0.0, 1.0) * 1000;
        std::cout << "Score!: " << score << ", diff: " << diff << ", sqrt: " << sqrt(diff) << "\n";
        return false;
    }
}

std::string ReadFile(std::string fileName) {
    std::ifstream file(fileName);
    if (!file.is_open()) {
        std::cerr << "Failed to open file \"" << fileName << "\"\n";
        return "";
    }

    std::ostringstream ss;
    ss << file.rdbuf(); // Read the whole file buffer into the stream
    return ss.str();
}

int main() {
    int port = 8080;

    Init();

    double score = 0;
    uWS::App().post("/guess", [&score](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        res->onAborted([]() {
            std::cout << "Request aborted\n";
        });

        res->onData([res, &score](std::string_view data, bool last) {
            if (last) {
                std::cout << "POST body: " << data << "\n";
                bool isCorrect = Guess(std::string(data), score);
                std::cout << "score thingy: " << score << "\n";

                res->writeHeader("Content-Type", "text/html");
                res->end(std::to_string(score));
            }
        });
    })
    .listen(port, [port](auto *listenSocket) {
        if (listenSocket) {
            std::cout << "Listening on " << port << "\n";
        }
    })
    .run();
    return 0;
} 

std::string RenderItem(const std::string& searchItemTpl) {
    ctemplate::Template* tpl = ctemplate::Template::GetTemplate(searchItemTpl, ctemplate::DO_NOT_STRIP);
    ctemplate::TemplateDictionary dict("item");
    // dict.SetValue("TITLE", item["title"].c_str());
    // dict.SetValue("URL", item["url"].c_str());
    // dict.SetValue("DESCRIPTION", item["description"].c_str());
    // dict.SetValue("FAVICON", item["favicon"].c_str());

    std::string output;
    tpl->Expand(&output, &dict);

    return output;
}
