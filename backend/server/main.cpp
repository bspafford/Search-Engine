#include <iostream>
#include <App.h>
#include <pqxx/pqxx>
#include <sstream>
#include <nlohmann/json.hpp>
#include <ctemplate/template.h>
#include <ctemplate/template.h>
#include <ctemplate/template_dictionary.h>
#include <ctemplate/template_enums.h>
#include "../login.h"

nlohmann::json ExecuteSQL(const std::string& query);

pqxx::connection cx("host=localhost dbname=SearchEngine user=" + USER + " password=" + PASSWORD);

void temp() {
    std::string input = "server/example.tpl";

    ctemplate::TemplateDictionary dict("example");
    dict.SetValue("NAME", "John Smith");
    int winnings = rand() % 100000;
    dict.SetIntValue("VALUE", winnings);
    dict.SetFormattedValue("TAXED_VALUE", "%.2f", winnings * 0.83);
    
    if (true) {
        dict.ShowSection("IN_CA");
    }

    std::string output;
    ctemplate::ExpandTemplate(input, ctemplate::DO_NOT_STRIP, &dict, &output);
    std::cout << output;
}

int main() {
    temp();
    return 0;
    int port = 8080;

    uWS::App().get("/search", [](uWS::HttpResponse<false> *res, uWS::HttpRequest *req) {

        std::string_view query = req->getQuery("q");
        std::cout << req->getUrl() << ", query: " << query << "\n";

        std::string output = ExecuteSQL(std::string(query));
        
        // std::stringstream ss;
        // ss << "<html><body><h1>Hello world!</h1><p>you search for \"" << query << "\"</p></body></html>";

        res->writeHeader("Content-Type", "text/html");
        res->end(output);
    })
    .listen(port, [port](auto *listenSocket) {
        if (listenSocket) {
            std::cout << "Listening on " << port << "\n";
        }
    })
    .run();
    return 0;

} 

nlohmann::json ExecuteSQL(const std::string& query) {
    // start a transaction
    pqxx::work tx{cx};

    std::stringstream output;

    std::string pattern = "%" + query + "%";
    // for (auto [id, url, title, description, contentHash, lastVisited] : tx.stream<long long, std::string_view, std::string_view, std::string_view, long long, std::string_view> (
        // "SELECT * FROM siteData WHERE title LIKE $1", pattern)) {
    auto result = tx.exec("SELECT * FROM siteData WHERE title ILIKE $1", pqxx::params(pattern));

    nlohmann::json json;
    for (auto row : result) {
         // output << "id: " << row["id"].c_str() << "\nurl: " << row["url"].c_str() << "\ntitle: " << row["title"].c_str() << "\ndescription: " << row["description"].c_str() << "\ncontentHash: " << row["contentHash"].c_str() << "\nlastVisited: " << row["lastVisited"].c_str() << "\n\n";
    }

    // Commit the transaction
    std::cout << "Making changes definite\n";
    tx.commit();
    std::cout << "OK\n";

    return output.str();
}
