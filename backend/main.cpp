#include <iostream>
#include <App.h>
#include <pqxx/pqxx>

void ExecuteSQL();

int main() {
    ExecuteSQL();
    return 0;

    uWS::App().post("/api/search", [](auto *res, auto *req) {
        // read the POST body
        res->onData([](std::string_view data, bool last) {
            if (last) {
                    std::cout << "Received: " << data << "\n";
            }
        });
        
        res->writeHeader("Content-Type", "application/json");
        res->end(R"({"results":["example result"]})");
    })
    .listen(8080, [](auto *listenSocket) {
        if (listenSocket) {
            std::cout << "Listening on 8080\n";
        }
    })
    .run();
    return 0;
} 


void ExecuteSQL() {
    // connect to db
    pqxx::connection cx("host=localhost dbname=SearchEngine user=ben password=password123");
    std::cout << "Connected to " << cx.dbname() << "\n";

    // start a transaction
    pqxx::work tx{cx};

    for (auto [id, url, title, description, contentHash, lastVisited] : tx.stream<long long, std::string_view, std::string_view, std::string_view, long long, std::string_view> (
        "SELECT * FROM siteData")) {

         std::cout << "id: " << id << "\nurl: " << url << "\ntitle: " << title << "\ndescription: " << description << "\ncontentHash: " << contentHash << "\nlastVisited: " << lastVisited << "\n\n";
    }

    // Commit the transaction
    std::cout << "Making changes definite\n";
    tx.commit();
    std::cout << "OK\n";
}
