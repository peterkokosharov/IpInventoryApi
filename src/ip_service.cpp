#include "ip_service.h"
#include "ip_utils.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
    const char* const kSuccessMessage = "Successful operation. OK";
    const std::size_t kMaxServiceIdLength = 255;
    const std::size_t kMaxItems = 1000;

    // Invalid input from the client -> HTTP 400.
    class BadRequest : public std::runtime_error
    {
    public:
        BadRequest(const std::string& code, const std::string& message)
            : std::runtime_error(message), code_(code)
        {
        }

        const std::string& code() const { return code_; }

    private:
        std::string code_;
    };

    crow::json::rvalue parseObject(const std::string& text)
    {
        crow::json::rvalue body = crow::json::load(text);

        if (!body)
            throw BadRequest("1000", "Request body is missing or not valid JSON");

        if (body.t() != crow::json::type::Object)
            throw BadRequest("1000", "Request body must be a JSON object");

        return body;
    }

    std::string getString(const crow::json::rvalue& obj, const std::string& key)
    {
        if (!obj.has(key) || obj[key].t() != crow::json::type::String)
            throw BadRequest("1002", "'" + key + "' is required and must be a string");

        std::string value = obj[key].s();
        return value;
    }

    std::string getServiceId(const crow::json::rvalue& obj, const std::string& key)
    {
        std::string value = getString(obj, key);

        if (value.empty() || value.size() > kMaxServiceIdLength)
            throw BadRequest("1002", "'" + key + "' must be 1-255 characters");

        return value;
    }

    const crow::json::rvalue& getList(const crow::json::rvalue& obj, const std::string& key)
    {
        if (!obj.has(key) || obj[key].t() != crow::json::type::List)
            throw BadRequest("1002", "'" + key + "' is required and must be an array");

        const crow::json::rvalue& list = obj[key];

        if (list.size() == 0)
            throw BadRequest("1002", "'" + key + "' must not be empty");

        if (list.size() > kMaxItems)
            throw BadRequest("1002", "'" + key + "' has too many entries");

        return list;
    }

    // Validates and normalizes one "ip" field of a list item.
    std::string parseIpField(const crow::json::rvalue& item, std::string& type)
    {
        if (item.t() != crow::json::type::Object)
            throw BadRequest("1002", "every ipAddresses entry must be an object");

        const std::string raw = getString(item, "ip");

        std::string canonical;
        if (!ip_utils::parse(raw, canonical, type))
            throw BadRequest("1001", "Invalid IP address: " + raw);

        return canonical;
    }

    // [{"ip": "..."}] -> normalized, de-duplicated IPs
    std::vector<std::string> parseIpList(const crow::json::rvalue& body)
    {
        const crow::json::rvalue& list = getList(body, "ipAddresses");

        std::vector<std::string> ips;
        for (const auto& item : list)
        {
            std::string type;
            std::string ip = parseIpField(item, type);

            if (std::find(ips.begin(), ips.end(), ip) == ips.end())
                ips.push_back(ip);
        }
        return ips;
    }
}

// =========================
// CONSTRUCTOR
// =========================
IpService::IpService()
{
    db = std::make_shared<Db>();
}

// =========================
// SUCCESS RESPONSES
// =========================
crow::response IpService::ok(const crow::json::wvalue& body)
{
    crow::response res(200);
    res.set_header("Content-Type", "application/json");
    res.write(body.dump());
    return res;
}

crow::response IpService::okStatus()
{
    crow::json::wvalue res;
    res["statusCode"] = "0";
    res["statusMessage"] = kSuccessMessage;
    return ok(res);
}

crow::response IpService::ipList(const std::vector<IpEntry>& ips)
{
    if (ips.empty())
    {
        // Always an array, never null.
        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.write("{\"ipAddresses\":[]}");
        return res;
    }

    crow::json::wvalue res;

    for (size_t i = 0; i < ips.size(); i++)
    {
        res["ipAddresses"][i]["ip"] = ips[i].first;
        res["ipAddresses"][i]["ipType"] = ips[i].second;
    }

    return ok(res);
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
// EXCEPTION -> RESPONSE MAPPING (one place for all handlers)
// =========================
crow::response IpService::guarded(const std::function<crow::response()>& fn)
{
    try
    {
        return fn();
    }
    catch (const BadRequest& e)
    {
        return error(e.code(), "Bad request", e.what(), 400);
    }
    catch (const BusinessError& e)
    {
        if (e.kind() == BusinessError::Kind::NotFound)
            return error(e.code(), "Not found", e.what(), 404);

        return error(e.code(), "Conflict", e.what(), 409);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return error("9000", "Internal error", "Unexpected server error", 500);
    }
    catch (...)
    {
        std::cerr << "[ERROR] unknown exception" << std::endl;
        return error("9000", "Internal error", "Unexpected server error", 500);
    }
}

// =========================
// ADD IP POOL
// =========================
crow::response IpService::addIpPool(const crow::request& req)
{
    return guarded([&]() {
        crow::json::rvalue body = parseObject(req.body);
        const crow::json::rvalue& list = getList(body, "ipAddresses");

        // Validate everything first; nothing is stored if one entry is bad.
        std::vector<IpEntry> entries;
        for (const auto& item : list)
        {
            std::string familyType;
            std::string ip = parseIpField(item, familyType);
            std::string ipType = getString(item, "ipType");

            if (!ip_utils::isValidIpType(ipType))
                throw BadRequest("1002", "'ipType' must be IPv4 or IPv6");

            if (ipType != familyType)
                throw BadRequest("1003", "ipType " + ipType + " does not match address " + ip);

            entries.push_back(IpEntry(ip, ipType));
        }

        db->addIps(entries);
        return okStatus();
    });
}

// =========================
// RESERVE IP
// =========================
crow::response IpService::reserveIp(const crow::request& req)
{
    return guarded([&]() {
        crow::json::rvalue body = parseObject(req.body);

        std::string serviceId = getServiceId(body, "serviceId");
        std::string ipType = getString(body, "ipType");

        if (ipType != "IPv4" && ipType != "IPv6" && ipType != "Both")
            throw BadRequest("1002", "'ipType' must be IPv4, IPv6 or Both");

        return ipList(db->reserveIps(serviceId, ipType));
    });
}

// =========================
// ASSIGN IP
// =========================
crow::response IpService::assignIp(const crow::request& req)
{
    return guarded([&]() {
        crow::json::rvalue body = parseObject(req.body);

        std::string serviceId = getServiceId(body, "serviceId");
        std::vector<std::string> ips = parseIpList(body);

        db->assignIps(serviceId, ips);
        return okStatus();
    });
}

// =========================
// TERMINATE IP
// =========================
crow::response IpService::terminateIp(const crow::request& req)
{
    return guarded([&]() {
        crow::json::rvalue body = parseObject(req.body);

        std::string serviceId = getServiceId(body, "serviceId");
        std::vector<std::string> ips = parseIpList(body);

        db->terminateIps(serviceId, ips);
        return okStatus();
    });
}

// =========================
// SERVICE ID CHANGE
// =========================
crow::response IpService::serviceIdChange(const crow::request& req)
{
    return guarded([&]() {
        crow::json::rvalue body = parseObject(req.body);

        std::string oldId = getServiceId(body, "serviceIdOld");
        std::string newId = getServiceId(body, "serviceId");

        if (oldId == newId)
            throw BadRequest("1002", "'serviceId' must differ from 'serviceIdOld'");

        db->changeServiceId(oldId, newId);
        return okStatus();
    });
}

// =========================
// GET BY SERVICE ID
// =========================
crow::response IpService::getByService(const crow::request& req)
{
    return guarded([&]() {
        const char* raw = req.url_params.get("serviceId");

        if (!raw || !*raw)
            throw BadRequest("5001", "Query parameter 'serviceId' is required");

        std::string serviceId = raw;
        if (serviceId.size() > kMaxServiceIdLength)
            throw BadRequest("5001", "'serviceId' must be 1-255 characters");

        return ipList(db->getByService(serviceId));
    });
}

// =========================
// EXPIRE RESERVATIONS
// =========================
void IpService::releaseExpiredReservations()
{
    db->releaseExpiredReservations();
}
