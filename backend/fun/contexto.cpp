#include <iostream>
#include <vector>
#include <random>
#include <algorithm>

#include "bot.h"

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

int main() {
    Bot::Init();

    std::vector<std::pair<double, std::string>> guesses;
    std::vector<std::string> randWordList = { "cat", "dog", "car", "plane", "house", "computer", "cloud" };

    std::string randWord = "house";//randWordList[randInt(0, randWordList.size() - 1)];
    std::cout << "Guess the word! (hint: " << randWord << ")\n";
    while (true) {
        std::string guess;
        std::cout << "Guess: ";
        std::getline(std::cin, guess);
        double diff = Bot::GetSimilarity(guess, randWord);
        if (guess == randWord) {
            std::cout << "\nYou got it!\n\n";
            break;
        } else {
            double score = std::clamp(sqrt(diff), 0.0, 1.0) * 1000;
            std::cout << "Score: " << score << ", diff: " << diff << "\n";
            guesses.push_back({ score, guess });
            OutputGuesses(guesses);
        }
    }
}
