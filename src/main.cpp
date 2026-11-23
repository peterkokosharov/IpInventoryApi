#include <crow.h>
#include "ip_service.h"
#include <memory>

int main()
{
    crow::SimpleApp app;
    auto service = std::make_shared<IpService>();

    // =========================
    // HEALTH CHECK
    // =========================
    CROW_ROUTE(app, "/health")
    ([](){
        return crow::response(200, "OK");
    });

    // =========================
    // SWAGGER UI
    // =========================
    CROW_ROUTE(app, "/swagger")
    ([](){
        std::ifstream file("static/swagger.html");
        if (!file.is_open())
            return crow::response(500, "Swagger UI not found");

        std::stringstream buffer;
        buffer << file.rdbuf();

        crow::response res(buffer.str());
        res.set_header("Content-Type", "text/html");
        return res;
    });

    CROW_ROUTE(app, "/swagger.yaml")
    ([](){
        std::ifstream file("static/swagger.yaml");
        if (!file.is_open())
            return crow::response(500, "Swagger YAML not found");

        std::stringstream buffer;
        buffer << file.rdbuf();

        crow::response res(buffer.str());
        res.set_header("Content-Type", "application/yaml");
        return res;
    });

    // =========================
    // API: ADD IP POOL
    // =========================
    CROW_ROUTE(app, "/ip-inventory/ip-pool").methods("POST"_method)
    ([service](const crow::request& req){
        return service->addIpPool(req);
    });

    // =========================
    // API: RESERVE IP
    // =========================
    CROW_ROUTE(app, "/ip-inventory/reserve-ip").methods("POST"_method)
    ([service](const crow::request& req){
        return service->reserveIp(req);
    });

    // =========================
    // API: ASSIGN IP
    // =========================
    CROW_ROUTE(app, "/ip-inventory/assign-ip-serviceId").methods("POST"_method)
    ([service](const crow::request& req){
        return service->assignIp(req);
    });

    // =========================
    // API: TERMINATE
    // =========================
    CROW_ROUTE(app, "/ip-inventory/terminate-ip-serviceId").methods("POST"_method)
    ([service](const crow::request& req){
        return service->terminateIp(req);
    });

    // =========================
    // API: SERVICE LOOKUP
    // =========================
    CROW_ROUTE(app, "/ip-inventory/serviceId").methods("GET"_method)
    ([service](const crow::request& req){
        return service->getByService(req);
    });

    // =========================
    // BACKGROUND CLEANER THREAD
    // =========================
    std::thread([service](){
        while (true)
        {
            service->releaseExpiredReservations();
            std::this_thread::sleep_for(std::chrono::minutes(1));
        }
    }).detach();

    app.port(18080).multithreaded().run();
}