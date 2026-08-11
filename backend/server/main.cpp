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
pqxx::result GetSites(pqxx::work& tx, const std::vector<std::string>& queries, int pageSize, int pageNum);

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

void Init() {
    cx.prepare(
        "get_sites",
        "WITH stats AS ("
            "SELECT "
                "COUNT(*) AS n, "
                "AVG(documentLength) AS avgdl "
            "FROM ( "
                "SELECT "
                    "id, "
                    "MAX(documentLength) AS documentLength "
                "FROM siteData "
                "GROUP BY id "
            ") d "
        "), "

        // weighted by fields
        "field_weights AS ("
            "SELECT * "
            "FROM (VALUES "
                "('title', 5.0::double precision), "
                "('h1', 4.0::double precision), "
                "('h2', 3.0::double precision), "
                "('h3', 2.0::double precision), "
                "('p', 1.0::double precision), "
                "('div', 0.5::double precision)"
            ") AS t(field, weight)"
        "), "

        "postings AS ( "
            "SELECT "
                "ii.wordId, "
                "ii.urlId, "
                "SUM(ii.count * field_weights.weight) AS tf, "

                "COUNT(*) OVER ( "
                    "PARTITION BY ii.wordId "
                ") AS df, "

                "sd.documentLength "

            "FROM inverted_index ii "

            "JOIN siteData sd "
                "ON sd.id = ii.urlId "
            "JOIN words w "
                "ON w.id = ii.wordId "
            "JOIN field_weights "
                "ON field_weights.field = ii.field "

            "WHERE w.word = ANY($1::text[])"
            "GROUP BY "
                "ii.wordId, "
                "ii.urlId, "
                "sd.documentLength "
        "), "

        "scores AS ("
            "SELECT "
                "urlId, "
                "SUM(LN((s.n - df + 0.5) / (df + 0.5) + 1) * (tf * ($2::double precision + 1)) / (tf + $2::double precision * (1 - $3::double precision + $3::double precision * documentLength / s.avgdl))) AS score "

            "FROM postings "
            "CROSS JOIN stats s "
            "GROUP BY urlId "
        ") "

        "SELECT "
            "scores.urlId, scores.score, sd.url, sd.title, sd.description, sd.favicon, COUNT(*) OVER () AS total_results "
        "FROM scores "
        "JOIN siteData sd ON sd.id = scores.urlId "
        "ORDER BY scores.score DESC "
        "LIMIT $4::integer "
        "OFFSET ($4::integer * ($5::integer - 1));"
    );
}

int main() {
    Init();

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
    dict.SetValue("DESCRIPTION", item["description"].c_str());

    // std::string tempDescription = "unique: " + item["keywords"].as<std::string>() + ", count: " + item["total_count"].as<std::string>() + ", authority: " + item["auth"].as<std::string>() + " = score: " + item["score"].as<std::string>() + "\n";
    // dict.SetValue("DESCRIPTION", tempDescription);

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

// BM25
pqxx::result GetSites(pqxx::work& tx, const std::vector<WordData>& queries, int pageSize, int pageNum) {
    // queries list to string
    std::string queryStr = "{";
    for (size_t i = 0; i < queries.size(); ++i) {
        if (i > 0) queryStr += ",";
        queryStr += pqxx::to_string(queries[i].word);
    }
    queryStr += "}";

    // pqxx::result result = tx.exec(sql, pqxx::params("{1817, 1823, 1796}", 1.25, 0.75));
    pqxx::result result = tx.exec(pqxx::prepped("get_sites"), pqxx::params(queryStr, 1.25, 0.75, pageSize, std::max(pageNum, 1)));

    return result;
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
    std::vector<WordData> words;
    Helper::ParseText(query, words, "");

    std::stringstream ss(query);
    std::string word;
    int wordCount;
    while (ss >> word)
        ++wordCount;

    int pageSize = 10;

    pqxx::result result = GetSites(tx, words, pageSize, pageNum);

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
