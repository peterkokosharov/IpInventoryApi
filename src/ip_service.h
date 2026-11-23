#pragma once

#include <crow.h>
#include "db.h"
#include <functional>
#include <string>
#include <vector>
#include <memory>

class IpService
{
public:
    IpService();

    // =========================
    // API HANDLERS
    // =========================
    crow::response addIpPool(const crow::request& req);

    crow::response reserveIp(const crow::request& req);

    crow::response assignIp(const crow::request& req);

    crow::response terminateIp(const crow::request& req);

    crow::response serviceIdChange(const crow::request& req);

    crow::response getByService(const crow::request& req);

    // =========================
    // BACKGROUND JOB
    // =========================
    void releaseExpiredReservations();

private:
    std::shared_ptr<Db> db;

    // =========================
    // INTERNAL HELPERS
    // =========================
    crow::response ok(const crow::json::wvalue& body);

    crow::response okStatus();

    crow::response ipList(const std::vector<IpEntry>& ips);

    crow::response error(
        const std::string& code,
        const std::string& message,
        const std::string& details,
        int httpStatus = 400
    );

    // Runs a handler body and maps exceptions to responses:
    // bad input -> 400, BusinessError -> 409 / 404, anything else -> 500.
    crow::response guarded(const std::function<crow::response()>& fn);
};
