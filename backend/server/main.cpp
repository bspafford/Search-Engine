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
#include "login.h"
#include "helper.h"

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
    dict.SetValue("FAVICON", item["favicon"].c_str());

    std::string output;
    tpl->Expand(&output, &dict);

    return output;
}

std::string ExecuteSQL(const std::string& query) {
    std::string searchPageTpl = "server/templates/searchPage.tpl";
    std::string searchItemTpl = "server/templates/searchItem.tpl";
    
    // start a transaction
    pqxx::work tx{cx};

    // get / parse search: remove special characters, uneeded words, same as function to put words into sql db
    // then select * WHERE word = search[i]
        // FROM urls u JOIN ... JOIN ... WHERE w.word IN ('...', '...', ...);
        // rank by who has most words from search term
    // rank in order of who has most words in search term
        // SELECT url.id COUNT(*) AS score ... GROUP BY uw.urlId ORDER BY score DESC;
        // and higher by who has a higher count from the search terms
    std::unordered_map<std::string, int> counts;
    Helper::ParseText(query, counts);
    std::string sql = "SELECT u.url, u.title, u.description, u.favicon, COUNT(DISTINCT w.word) AS matched_words, SUM(ii.count) AS frequency "
                      "FROM words w "
                      "JOIN inverted_index ii ON w.id = ii.wordid "
                      "JOIN siteData u ON ii.urlid = u.id "
                      "WHERE w.word IN (";

    pqxx::params params;
    bool first = true;
    int i = 0;
    for (const auto& [word, _] : counts) {
        if (!first)
            sql += ", ";
        first = false;

        sql += "$" + std::to_string(++i);
        params.append(word);
    }

    sql += ") "
           "GROUP BY u.id, u.url "
           "ORDER BY matched_words DESC, frequency DESC;";

    std::cout << "sql:\n" << sql << "\n\n";

    pqxx::result result = tx.exec(sql, params);

    ctemplate::TemplateDictionary dict("search");
    ctemplate::Template* tpl = ctemplate::Template::GetTemplate(searchPageTpl, ctemplate::DO_NOT_STRIP);

    std::cout << "result:\n";
    for (auto row : result) {
        for (auto field : row)
            std::cout << field.c_str() << "\t";
        std::cout << "\n";
    }
    std::cout << "\n\n";

    std::string itemHtml;
    std::cout << "found " << result.size() << " result(s)\n";
    for (pqxx::row_ref row : result) {
        itemHtml += RenderItem(row, searchItemTpl);
    }

    dict.SetValue("ITEMS", itemHtml);

    std::string output;
    tpl->Expand(&output, &dict);

    // Commit the transaction
    tx.commit();

    return output;
}
