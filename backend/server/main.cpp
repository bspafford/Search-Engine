#include <iostream>
#include <App.h>
#include <pqxx/pqxx>
#include <sstream>
#include "../login.h"

std::string ExecuteSQL();

pqxx::connection cx("host=localhost dbname=SearchEngine user=" + USER + " password=" + PASSWORD);

int main() {
    uWS::App().post("/api/search", [](auto *res, auto *req) {
        // read the POST body
        res->onData([](std::string_view data, bool last) {
            if (last) {
                    std::cout << "Received: " << data << "\n";
            }
        });

        std::string str = ExecuteSQL();
        
        res->writeHeader("Content-Type", "application/text");
        res->end(str);
    })
    .listen(8080, [](auto *listenSocket) {
        if (listenSocket) {
            std::cout << "Listening on 8080\n";
        }
    })
    .run();
    return 0;
} 


std::string ExecuteSQL() {
    // start a transaction
    pqxx::work tx{cx};

    std::stringstream output;

    for (auto [id, url, title, description, contentHash, lastVisited] : tx.stream<long long, std::string_view, std::string_view, std::string_view, long long, std::string_view> (
        "SELECT * FROM siteData")) {

         output << "id: " << id << "\nurl: " << url << "\ntitle: " << title << "\ndescription: " << description << "\ncontentHash: " << contentHash << "\nlastVisited: " << lastVisited << "\n\n";
    }

    // Commit the transaction
    std::cout << "Making changes definite\n";
    tx.commit();
    std::cout << "OK\n";

    return output.str();
}
