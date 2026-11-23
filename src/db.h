#pragma once

#include <soci/soci.h>
#include <soci/connection-pool.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// A rule of the inventory was violated (not a technical failure).
// Never retried; the HTTP layer maps it to 409 (Conflict) or 404 (NotFound).
class BusinessError : public std::runtime_error
{
public:
    enum class Kind
    {
        Conflict,
        NotFound
    };

    BusinessError(Kind kind, const std::string& code, const std::string& message)
        : std::runtime_error(message), kind_(kind), code_(code)
    {
    }

    Kind kind() const { return kind_; }
    const std::string& code() const { return code_; }

private:
    Kind kind_;
    std::string code_;
};

using IpEntry = std::pair<std::string, std::string>; // (ip, ipType)

// All IP arguments must already be validated and normalized (see ip_utils.h).
// Every mutating method is ONE transaction, serialized by one advisory lock,
// so a request is all-or-nothing and the cleaner can not race with it.
class Db
{
public:
    Db();

    // Adds IPs to the pool (already present IPs are left unchanged).
    void addIps(const std::vector<IpEntry>& ips);

    // type is "IPv4", "IPv6" or "Both". Idempotent: IPs the serviceId has
    // already reserved are returned instead of reserving new ones.
    // Throws BusinessError(Conflict) when no free IP is left.
    std::vector<IpEntry> reserveIps(const std::string& serviceId, const std::string& type);

    // Reserved (and not expired) for this serviceId -> assigned.
    void assignIps(const std::string& serviceId, const std::vector<std::string>& ips);

    void terminateIps(const std::string& serviceId, const std::vector<std::string>& ips);

    // Moves assignments and reservations from the old to the new serviceId.
    void changeServiceId(const std::string& oldId, const std::string& newId);

    std::vector<IpEntry> getByService(const std::string& serviceId);

    // Frees reservations older than the TTL (called by the cleaner job).
    void releaseExpiredReservations();

private:
    // Each operation takes its own session from the pool,
    // so request threads and the cleaner thread never share one.
    soci::connection_pool pool;
    int ttlSeconds;
};
