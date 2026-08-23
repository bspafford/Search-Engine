#include <iostream>
#include <App.h>
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>
#include <ctemplate/template.h>
#include <ctemplate/template.h>
#include <ctemplate/template_dictionary.h>
#include <ctemplate/template_enums.h>
#include "login.h"

void Init();
nlohmann::json ResultToJson(const pqxx::result& result);

pqxx::connection cx("host=localhost dbname=wikirace user=" + USER + " password=" + PASSWORD);

int main() {
    int port = 8080;

    Init();

    uWS::App().get("/search", [](uWS::HttpResponse<false> *res, uWS::HttpRequest *req) {

        std::cout << "called api!\n";

        pqxx::work tx{cx};
        pqxx::result result = tx.exec(pqxx::prepped("getTopX"), pqxx::params(10));
        tx.commit();

        // pqxx::result to nlohmann::json
        nlohmann::json json = ResultToJson(result);

        res->writeHeader("Content-Type", "application/json");
        res->end(json.dump());
    })
    .listen(port, [port](auto *listenSocket) {
        if (listenSocket) {
            std::cout << "Listening on " << port << "\n";
        }
    })
    .get("/getData", [](uWS::HttpResponse<false> *res, uWS::HttpRequest *req) {
        std::cout << "getting data\n";

        pqxx::work tx{cx};
        pqxx::result result = tx.exec(pqxx::prepped("getData"));
        tx.commit();

        nlohmann::json json = ResultToJson(result);

        res->writeHeader("Content-Type", "application/json");
        res->end(json.dump());
    })
    .get("/getConnections", [](uWS::HttpResponse<false> *res, uWS::HttpRequest *req) {
        std::cout << "getting connections\n";

        std::string_view limit = req->getQuery("limit");
        std::string_view offset = req->getQuery("offset");

        pqxx::work tx{cx};
        pqxx::result result = tx.exec(pqxx::prepped("getConnections"), pqxx::params(limit, offset));
        tx.commit();

        nlohmann::json json = ResultToJson(result);

        res->writeHeader("Content-Type", "application/json");
        res->end(json.dump());
    })
    .get("/getConnCount", [](uWS::HttpResponse<false> *res, uWS::HttpRequest *req) {
        pqxx::work tx{cx};
        pqxx::result result = tx.exec("SELECT COUNT(*) FROM connections");
        tx.commit();

        res->writeHeader("Content-Type", "text/plain");
        res->end(result.one_field().as<std::string>());
    })
    .run();
    return 0;

}

void Init() {
    cx.prepare("getTopX", "SELECT * FROM wiki LIMIT $1");

    cx.prepare("getData",
        "SELECT id, path, title, authority FROM wiki"
    );

    cx.prepare("getConnections",
        "SELECT * FROM connections LIMIT $1 OFFSET $2"
    );
}

nlohmann::json ResultToJson(const pqxx::result& result) {
    nlohmann::json json = nlohmann::json::array();
    for (const pqxx::row_ref row : result) {
        nlohmann::json jRow = nlohmann::json::object();

        for (const pqxx::field_ref col : row)
            jRow[col.name()] = col.is_null() ? nullptr : col.c_str();

        json.push_back(jRow);
    }

    return json;
}
