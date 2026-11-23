#include "db.h"
#include <soci/postgresql/soci-postgresql.h>
#include <chrono>
#include <thread>
#include <iostream>

// =========================
// RETRY WRAPPER (TRANSACTION SAFE)
// =========================
namespace
{
    template <typename Func>
    void executeWithRetry(Func f, int maxRetries = 3)
    {
        int attempt = 0;

        while (attempt < maxRetries)
        {
            try
            {
                f();
                return;
            }
            catch (const std::exception& e)
            {
                attempt++;

                std::cerr << "[DB RETRY] attempt " << attempt
                          << " failed: " << e.what() << std::endl;

                if (attempt >= maxRetries)
                    throw;

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(50 * attempt)
                );
            }
        }
    }
}

// =========================
// CONSTRUCTOR
// =========================
Db::Db()
    : sql(soci::postgresql,
          "dbname=ip_inventory user=postgres password=postgres host=localhost port=5432")
{
}

// =========================
// ADD IP TO POOL
// =========================
void Db::addIp(const std::string& ip, const std::string& type)
{
    executeWithRetry([&]() {
        soci::transaction tr(sql);

        sql << "INSERT INTO ip_pool(ip, ip_type, status) "
               "VALUES(:ip, :type, 'FREE') "
               "ON CONFLICT(ip) DO NOTHING",
            soci::use(ip), soci::use(type);

        tr.commit();
    });
}

// =========================
// SOCI already provides soci::row, but it does not define an empty() method in the official API.
// =========================
inline bool empty(const soci::row& r)
{
    return r.size() == 0;
}

// =========================
// RESERVE IP (DISTRIBUTED SAFE)
// =========================
std::vector<std::pair<std::string, std::string>>
Db::reserveIp(const std::string& serviceId, const std::string& type)
{
    std::vector<std::pair<std::string, std::string>> result;

    executeWithRetry([&]() {
        soci::transaction tr(sql);

        // Global safety lock (optional but useful under heavy load)
        sql << "SELECT pg_advisory_xact_lock(hashtext('reserve-ip-global'))";

        soci::row row;
        bool found = false;

        if (type == "IPv4" || type == "IPv6")
        {
            sql << "SELECT ip, ip_type FROM ip_pool "
                   "WHERE status='FREE' AND ip_type=:type "
                   "LIMIT 1",
                soci::use(type), soci::into(row);
        }
        else
        {
            sql << "SELECT ip, ip_type FROM ip_pool "
                   "WHERE status='FREE' "
                   "LIMIT 1",
                soci::into(row);
        }

        if (!empty(row))
        {
            std::string ip = row.get<std::string>(0);
            std::string ipType = row.get<std::string>(1);

            // Per-IP lock (prevents double reservation)
            sql << "SELECT pg_advisory_xact_lock(hashtext(:ip))",
                soci::use(ip);

            // Double-check state (idempotency guard)
            int count = 0;
            sql << "SELECT COUNT(*) FROM ip_pool "
                   "WHERE ip=:ip AND status='FREE'",
                soci::use(ip), soci::into(count);

            if (count == 0)
                throw std::runtime_error("IP already reserved/assigned");

            // Reserve
            sql << "UPDATE ip_pool SET status='RESERVED' WHERE ip=:ip",
                soci::use(ip);

            sql << "INSERT INTO ip_reservations(ip, service_id, reserved_at) "
                   "VALUES(:ip, :sid, NOW())",
                soci::use(ip), soci::use(serviceId);

            result.push_back({ip, ipType});
            found = true;
        }

        if (!found)
            throw std::runtime_error("No available IPs");

        tr.commit();
    });

    return result;
}



// =========================
// CHECK RESERVED
// =========================
bool Db::isReserved(const std::string& ip, const std::string& serviceId)
{
    int count = 0;

    sql << "SELECT COUNT(*) FROM ip_reservations "
           "WHERE ip=:ip AND service_id=:sid",
        soci::use(ip), soci::use(serviceId), soci::into(count);

    return count > 0;
}

// =========================
// ASSIGN IP (FULL SAFETY)
// =========================
void Db::assignIp(const std::string& ip, const std::string& serviceId)
{
    executeWithRetry([&]() {
        soci::transaction tr(sql);

        // Per-IP distributed lock
        sql << "SELECT pg_advisory_xact_lock(hashtext(:ip))",
            soci::use(ip);

        // Prevent double assignment
        int count = 0;
        sql << "SELECT COUNT(*) FROM ip_assignments "
               "WHERE ip=:ip AND active=TRUE",
            soci::use(ip), soci::into(count);

        if (count > 0)
            throw std::runtime_error("IP already assigned");

        // Ensure it was reserved
        sql << "UPDATE ip_pool SET status='ASSIGNED' WHERE ip=:ip",
            soci::use(ip);

        sql << "INSERT INTO ip_assignments(ip, service_id, assigned_at, active) "
               "VALUES(:ip, :sid, NOW(), TRUE)",
            soci::use(ip), soci::use(serviceId);

        // Remove reservation (idempotent cleanup)
        sql << "DELETE FROM ip_reservations "
               "WHERE ip=:ip AND service_id=:sid",
            soci::use(ip), soci::use(serviceId);

        tr.commit();
    });
}

// =========================
// CHECK ASSIGNED
// =========================
bool Db::isAssigned(const std::string& ip, const std::string& serviceId)
{
    int count = 0;

    sql << "SELECT COUNT(*) FROM ip_assignments "
           "WHERE ip=:ip AND service_id=:sid AND active=TRUE",
        soci::use(ip), soci::use(serviceId), soci::into(count);

    return count > 0;
}

// =========================
// TERMINATE IP
// =========================
void Db::terminateIp(const std::string& ip, const std::string& serviceId)
{
    executeWithRetry([&]() {
        soci::transaction tr(sql);

        sql << "SELECT pg_advisory_xact_lock(hashtext(:ip))",
            soci::use(ip);

        sql << "UPDATE ip_pool SET status='FREE' WHERE ip=:ip",
            soci::use(ip);

        sql << "UPDATE ip_assignments "
               "SET active=FALSE "
               "WHERE ip=:ip AND service_id=:sid",
            soci::use(ip), soci::use(serviceId);

        tr.commit();
    });
}

// =========================
// GET BY SERVICE
// =========================
std::vector<std::pair<std::string, std::string>>
Db::getByService(const std::string& serviceId)
{
    std::vector<std::pair<std::string, std::string>> result;

    soci::rowset<soci::row> rs =
        (sql.prepare <<
            "SELECT p.ip, p.ip_type "
            "FROM ip_pool p "
            "JOIN ip_assignments a ON a.ip=p.ip "
            "WHERE a.service_id=:sid AND a.active=TRUE",
            soci::use(serviceId));

    for (auto it = rs.begin(); it != rs.end(); ++it)
    {
        result.push_back({
            it->get<std::string>(0),
            it->get<std::string>(1)
        });
    }

    return result;
}

// =========================
// RELEASE EXPIRED RESERVATIONS
// =========================
void Db::releaseExpiredReservations()
{
    executeWithRetry([&]() {
        soci::transaction tr(sql);

        // Global lock so only ONE node cleans at a time
        sql << "SELECT pg_advisory_xact_lock(hashtext('cleanup-expired-reservations'))";

        // Free expired IPs safely
        sql << "UPDATE ip_pool "
               "SET status='FREE' "
               "WHERE ip IN ("
               "   SELECT ip FROM ip_reservations "
               "   WHERE reserved_at < NOW() - INTERVAL '15 minutes'"
               ")";

        // Remove expired reservations (idempotent)
        sql << "DELETE FROM ip_reservations "
               "WHERE reserved_at < NOW() - INTERVAL '15 minutes'";

        tr.commit();
    });
}