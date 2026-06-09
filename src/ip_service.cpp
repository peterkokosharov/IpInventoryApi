#include "ip_service.h"
#include <sstream>
#include <regex>

// =========================
// CONSTRUCTOR
// =========================
IpService::IpService()
{
    db = std::make_shared<Db>();
}

// =========================
// SUCCESS RESPONSE
// =========================
crow::response IpService::ok(const crow::json::wvalue& body)
{
    crow::response res(200);
    res.set_header("Content-Type", "application/json");
    res.write(body.dump());
    return res;
}

// =========================
// ERROR RESPONSE (STANDARDIZED)
// =========================
crow::response IpService::error(
    const std::string& code,
    const std::string& message,
    const std::string& details,
    int httpStatus)
{
    crow::json::wvalue err;
    err["statusCode"] = code;
    err["statusMessage"] = message;
    err["details"] = details;

    crow::response res(httpStatus);
    res.set_header("Content-Type", "application/json");
    res.write(err.dump());
    return res;
}

// =========================
// IP VALIDATION
// =========================
bool IpService::isValidIp(const std::string& ip)
{
    // very simple IPv4/IPv6 check (can be improved later)
    std::regex ipv4("(([0-9]|[1-9][0-9]|1[0-9][0-9]|2[0-4][0-9]|25[0-5])\\.){3}([0-9]|[1-9][0-9]|1[0-9][0-9]|2[0-4][0-9]|25[0-5])");

    std::regex ipv6("((([0-9a-fA-F]){1,4})\\:){7}([0-9a-fA-F]){1,4}");

    return std::regex_match(ip, ipv4) || std::regex_match(ip, ipv6);
}



// =========================
// ADD IP POOL
// =========================
crow::response IpService::addIpPool(const crow::request& req)
{
    auto body = crow::json::load(req.body);

    if (!body)
        return error("1000", "Invalid JSON", "Request body is not valid JSON");

    for (const auto& item : body["ipAddresses"])
    {
        std::string ip = item["ip"].s();
        std::string type = item["ipType"].s();

        if (!isValidIp(ip))
            return error("1001", "Invalid IP", ip);

        db->addIp(ip, type);
    }

    crow::json::wvalue res;
    res["statusCode"] = "0";
    res["statusMessage"] = "Successful operation";

    return ok(res);
}

// =========================
// RESERVE IP
// =========================
crow::response IpService::reserveIp(const crow::request& req)
{
    auto body = crow::json::load(req.body);

    std::string serviceId = body["serviceId"].s();
    std::string ipType = body["ipType"].s();

    auto ips = db->reserveIp(serviceId, ipType);

    if (ips.empty())
        return error("2001", "No available IPs", "Pool exhausted", 409);

    crow::json::wvalue res;

    for (size_t i = 0; i < ips.size(); i++)
    {
        res["ipAddresses"][i]["ip"] = ips[i].first;
        res["ipAddresses"][i]["ipType"] = ips[i].second;
    }

    return ok(res);
}

// =========================
// ASSIGN IP
// =========================
crow::response IpService::assignIp(const crow::request& req)
{
    auto body = crow::json::load(req.body);

    std::string serviceId = body["serviceId"].s();

    for (const auto& item : body["ipAddresses"])
    {
        std::string ip = item["ip"].s();

        // RULE: must be reserved first
        if (!db->isReserved(ip, serviceId))
            return error(
                "3001",
                "Invalid state transition",
                "IP must be RESERVED before assignment"
            );

        db->assignIp(ip, serviceId);
    }

    crow::json::wvalue res;
    res["statusCode"] = "0";
    res["statusMessage"] = "Successful operation";

    return ok(res);
}

// =========================
// TERMINATE IP
// =========================
crow::response IpService::terminateIp(const crow::request& req)
{
    auto body = crow::json::load(req.body);

    std::string serviceId = body["serviceId"].s();

    for (const auto& item : body["ipAddresses"])
    {
        std::string ip = item["ip"].s();

        if (!db->isAssigned(ip, serviceId))
            return error(
                "4001",
                "IP not assigned",
                "Cannot terminate non-assigned IP"
            );

        db->terminateIp(ip, serviceId);
    }

    crow::json::wvalue res;
    res["statusCode"] = "0";
    res["statusMessage"] = "Successful operation";

    return ok(res);
}

// =========================
// GET BY SERVICE ID
// =========================
crow::response IpService::getByService(const crow::request& req)
{
    auto serviceId = req.url_params.get("serviceId");

    if (!serviceId)
        return error("5001", "Missing parameter", "serviceId is required");

    auto ips = db->getByService(serviceId);

    crow::json::wvalue res;

    for (size_t i = 0; i < ips.size(); i++)
    {
        res["ipAddresses"][i]["ip"] = ips[i].first;
        res["ipAddresses"][i]["ipType"] = ips[i].second;
    }

    return ok(res);
}

// =========================
// EXPIRE RESERVATIONS
// =========================
void IpService::releaseExpiredReservations()
{
    db->releaseExpiredReservations();
}