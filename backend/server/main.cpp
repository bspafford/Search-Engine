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

std::string ExecuteSQL(const std::string& query, int pageNum);

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

int safeSTOI(std::string num) {
    try {
        return std::stoi(num);
    } catch (...) {
        return 1; // default
    }
}

int main() {
    int port = 8080;

    uWS::App().get("/search", [](uWS::HttpResponse<false> *res, uWS::HttpRequest *req) {

        std::string_view query = req->getQuery("q");
        std::string_view page = req->getQuery("page");
        std::cout << req->getUrl() << ", query: " << query << ", page: " << page << "\n";

        std::string output = ExecuteSQL(std::string(query), safeSTOI(std::string(page)));

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

    pqxx::field_ref favicon = item["favicon"];

    dict.SetValue("TITLE", item["title"].c_str());
    dict.SetValue("URL", item["url"].c_str());
    // dict.SetValue("DESCRIPTION", item["description"].c_str());

    std::string tempDescription = "unique: " + item["keywords"].as<std::string>() + ", count: " + item["total_count"].as<std::string>() + ", authority: " + item["auth"].as<std::string>() + " = score: " + item["score"].as<std::string>() + "\n";
    dict.SetValue("DESCRIPTION", tempDescription);

    dict.SetValue("FAVICON", favicon.is_null() ? "default.png" : favicon.as<std::string>());

    std::string output;
    tpl->Expand(&output, &dict);

    return output;
}

std::string RenderPagination(const std::string& q, int page, const std::string& paginationTpl) {
    ctemplate::Template* tpl = ctemplate::Template::GetTemplate(paginationTpl, ctemplate::DO_NOT_STRIP);
    ctemplate::TemplateDictionary dict("item");

    dict.SetValue("QUERY", q);
    dict.SetValue("PAGE", std::to_string(page + 1));

    std::string output;
    tpl->Expand(&output, &dict);

    return output;
}

std::string ExecuteSQL(const std::string& query, int pageNum) {
    std::string searchPageTpl = "server/templates/searchPage.tpl";
    std::string searchItemTpl = "server/templates/searchItem.tpl";
    std::string paginationTpl = "server/templates/pagination.tpl";

    // start a transaction
    pqxx::work tx{cx};

    // get / parse search: remove special characters, uneeded words, same as function to put words into sql db
    // then select * WHERE word = search[i]
        // FROM urls u JOIN ... JOIN ... WHERE w.word IN ('...', '...', ...);
        // rank by who has most words from search term
    // rank in order of who has most words in search term
        // SELECT url.id COUNT(*) AS score ... GROUP BY uw.urlId ORDER BY score DESC;
        // and higher by who has a higher count from the search terms
    std::vector<std::string> words;
    Helper::ParseText(query, words);

    std::stringstream ss(query);
    std::string word;
    int wordCount;
    while (ss >> word)
        ++wordCount;

    int pageSize = 10;

    // score should be weighted: w1 * keyword + w2 * semantic + ...
    // keyword, semantic, title, url, authority, freshness, quality, spam, duplicate
    std::string sql = "SELECT *, "
                          "(keywords + total_count + auth) AS score, " // score
                          "COUNT(*) OVER () AS total_results "
                      "FROM ("
                          "SELECT u.url, u.title, u.description, u.favicon, u.authority, "

                          "(0.35 * COUNT(DISTINCT w.word) / $1) AS keywords, "
                          "(0.35 * SUM(ii.count)) AS total_count, " // should normalize
                          "(0.3 * LN(1 + u.authority)) AS auth "


                          "FROM words w "
                          "JOIN inverted_index ii ON w.id = ii.wordid "
                          "JOIN siteData u ON ii.urlid = u.id "
                          "WHERE w.word IN (";

    pqxx::params params;
    params.append(wordCount);
    bool first = true;
    int i = 1;
    for (const std::string& word : words) { // counts
        if (!first)
            sql += ", ";
        first = false;

        sql += "$" + std::to_string(++i);
        params.append(word);
    }

    sql += ") "
           "GROUP BY u.id, u.url "
           ") AS ranked "
           "ORDER BY score DESC "
           "LIMIT $" + std::to_string(i + 1) + " OFFSET $" + std::to_string(i + 2) + ";";

    params.append(pageSize);
    params.append(pageSize * (pageNum - 1));

    pqxx::result result = tx.exec(sql, params);

    ctemplate::TemplateDictionary dict("search");
    ctemplate::Template* tpl = ctemplate::Template::GetTemplate(searchPageTpl, ctemplate::DO_NOT_STRIP);

    std::string itemHtml;
    int totalCount = result.size() ? result[0]["total_results"].as<int>() : 0;

    std::cout << "found " << totalCount << " result(s)\n";
    for (pqxx::row_ref row : result) {
        itemHtml += RenderItem(row, searchItemTpl);
    }

    std::string paginationHtml;
    for (int i = 0; i < std::ceil(totalCount / float(pageSize)); ++i) {
        paginationHtml += RenderPagination(query, i, paginationTpl);
    }

    dict.SetValue("ITEMS", itemHtml);
    dict.SetValue("PAGINATION", paginationHtml);

    std::string output;
    tpl->Expand(&output, &dict);

    // Commit the transaction
    tx.commit();

    return output;
}
