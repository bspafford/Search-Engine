#include <iostream>
#include <App.h>
#include <pqxx/pqxx>
#include <sstream>
#include <nlohmann/json.hpp>
#include <ctemplate/template.h>
#include <ctemplate/template.h>
#include <ctemplate/template_dictionary.h>
#include <ctemplate/template_enums.h>
#include <fstream>
#include "../login.h"

std::string ExecuteSQL(const std::string& query);

pqxx::connection cx("host=localhost dbname=SearchEngine user=" + USER + " password=" + PASSWORD);

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

    uWS::App().get("/search", [](uWS::HttpResponse<false> *res, uWS::HttpRequest *req) {

        std::string_view query = req->getQuery("q");
        std::cout << req->getUrl() << ", query: " << query << "\n";

        std::string output = ExecuteSQL(std::string(query));
        
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

std::string RenderItem(pqxx::row_ref item, const std::string& searchItemTpl) {
    ctemplate::Template* tpl = ctemplate::Template::GetTemplate(searchItemTpl, ctemplate::DO_NOT_STRIP);
    ctemplate::TemplateDictionary dict("item");
    dict.SetValue("TITLE", item["title"].c_str());
    dict.SetValue("URL", item["url"].c_str());
    dict.SetValue("DESCRIPTION", item["description"].c_str());

    std::string output;
    tpl->Expand(&output, &dict);

    std::cout << "item: " << output << "\n";
    return output;
}

std::string ExecuteSQL(const std::string& query) {
    std::string searchPageTpl = "server/templates/searchPage.tpl";
    std::string searchItemTpl = "server/templates/searchItem.tpl";
    
    // start a transaction
    pqxx::work tx{cx};

    std::string pattern = "%" + query + "%";
    auto result = tx.exec("SELECT * FROM siteData WHERE title ILIKE $1", pqxx::params(pattern));

    ctemplate::TemplateDictionary dict("search");
    ctemplate::Template* tpl = ctemplate::Template::GetTemplate(searchPageTpl, ctemplate::DO_NOT_STRIP);

    std::string itemHtml;
    std::cout << "found " << result.size() << " result(s)\n";
    for (pqxx::row_ref row : result) {
        itemHtml += RenderItem(row, searchItemTpl);
    }

    dict.SetValue("ITEMS", itemHtml);

    std::string output;
    tpl->Expand(&output, &dict);

    // Commit the transaction
    std::cout << "Making changes definite\n";
    tx.commit();
    std::cout << "OK\n";

    return output;
}
