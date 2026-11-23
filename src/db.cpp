#include "db.h"
#include <soci/postgresql/soci-postgresql.h>
#include <chrono>
#include <thread>
#include <iostream>
#include "config.h"

namespace
{
    const std::size_t kPoolSize = 8;
}

// =========================
// RETRY WRAPPER (TRANSACTION SAFE)
// =========================
// Only serialization failures and deadlocks are retried. Business errors
// and every other failure are rethrown at once.
namespace
{
    template <typename Func>
    void executeWithRetry(Func f, int maxRetries = 3)
    {
        for (int attempt = 1; ; attempt++)
        {
            try
            {
                f();
                return;
            }
            catch (const BusinessError&)
            {
                throw;
            }
            catch (const soci::postgresql_soci_error& e)
            {
                const std::string state = e.sqlstate();
                const bool retryable = (state == "40001" || state == "40P01");

                if (!retryable || attempt >= maxRetries)
                    throw;

                std::cerr << "[DB RETRY] attempt " << attempt
                          << " failed (" << state << "): " << e.what() << std::endl;

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(50 * attempt)
                );
            }
        }
    }

    // One global advisory lock serializes every mutating transaction.
    // It is taken first in each transaction (released at commit/rollback),
    // which makes check-then-change sequences safe and keeps the cleaner
    // from racing with reserve/assign. Throughput is limited on purpose.
    void lockInventory(soci::session& sql)
    {
        sql << "SELECT pg_advisory_xact_lock(727001)";
    }

    std::string ttlInterval(int ttlSeconds)
    {
        return "INTERVAL '" + std::to_string(ttlSeconds) + " seconds'";
    }

    // Frees reservations older than the TTL. Must run inside a transaction
    // that already holds the inventory lock.
    void releaseExpiredLocked(soci::session& sql, int ttlSeconds)
    {
        const std::string cutoff = "NOW() - " + ttlInterval(ttlSeconds);

        sql << ("UPDATE ip_pool SET status='FREE' "
                "WHERE status='RESERVED' AND ip IN ("
                "   SELECT ip FROM ip_reservations WHERE reserved_at <= " + cutoff + ")");

        sql << ("DELETE FROM ip_reservations WHERE reserved_at <= " + cutoff);
    }

    bool inPool(soci::session& sql, const std::string& ip)
    {
        int count = 0;
        sql << "SELECT COUNT(*) FROM ip_pool WHERE ip=:ip",
            soci::use(ip), soci::into(count);
        return count > 0;
    }
}

// =========================
// CONSTRUCTOR
// =========================
Db::Db()
    : pool(kPoolSize), ttlSeconds(config::reservationTtlSeconds())
{
    const std::string conn = config::dbConn();
    const int maxAttempts = 30;

    // Wait for the database instead of crashing when it is not up yet.
    for (int attempt = 1; ; attempt++)
    {
        try
        {
            for (std::size_t i = 0; i < kPoolSize; i++)
                pool.at(i).open(soci::postgresql, conn);
            break;
        }
        catch (const std::exception& e)
        {
            // open() refuses an already open session, so reset before retrying
            for (std::size_t i = 0; i < kPoolSize; i++)
            {
                try { pool.at(i).close(); } catch (...) {}
            }

            std::cerr << "[DB CONNECT] attempt " << attempt << "/" << maxAttempts
                      << " failed: " << e.what() << std::endl;

            if (attempt >= maxAttempts)
                throw;

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

// =========================
// ADD IPS TO POOL
// =========================
void Db::addIps(const std::vector<IpEntry>& ips)
{
    soci::session sql(pool);
    executeWithRetry([&]() {
        soci::transaction tr(sql);
        lockInventory(sql);

        for (const auto& entry : ips)
        {
            const std::string ip = entry.first;
            const std::string type = entry.second;

            sql << "INSERT INTO ip_pool(ip, ip_type, status) "
                   "VALUES(:ip, :type, 'FREE') "
                   "ON CONFLICT(ip) DO NOTHING",
                soci::use(ip), soci::use(type);
        }

        tr.commit();
    });
}

// =========================
// RESERVE IPS (one IPv4 and/or one IPv6, all or nothing)
// =========================
std::vector<IpEntry>
Db::reserveIps(const std::string& serviceId, const std::string& type)
{
    std::vector<IpEntry> result;

    std::vector<std::string> wanted;
    if (type == "IPv4" || type == "Both")
        wanted.push_back("IPv4");
    if (type == "IPv6" || type == "Both")
        wanted.push_back("IPv6");

    soci::session sql(pool);
    executeWithRetry([&]() {
        result.clear();

        soci::transaction tr(sql);
        lockInventory(sql);

        // An expired reservation must not block the pool until the cleaner runs.
        releaseExpiredLocked(sql, ttlSeconds);

        for (const std::string& t : wanted)
        {
            // Idempotency: already reserved for this serviceId?
            bool have = false;
            {
                soci::rowset<soci::row> rs =
                    (sql.prepare <<
                        "SELECT p.ip FROM ip_reservations r "
                        "JOIN ip_pool p ON p.ip = r.ip "
                        "WHERE r.service_id=:sid AND p.ip_type=:type AND p.status='RESERVED' "
                        "ORDER BY p.ip",
                        soci::use(serviceId), soci::use(t));

                for (auto it = rs.begin(); it != rs.end(); ++it)
                {
                    result.push_back(IpEntry(it->get<std::string>(0), t));
                    have = true;
                }
            }
            if (have)
                continue;

            // Take one free IP of this type.
            std::string ip;
            soci::statement st =
                (sql.prepare <<
                    "SELECT ip FROM ip_pool "
                    "WHERE status='FREE' AND ip_type=:type "
                    "ORDER BY ip LIMIT 1 FOR UPDATE SKIP LOCKED",
                    soci::use(t), soci::into(ip));

            if (!st.execute(true))
                throw BusinessError(BusinessError::Kind::Conflict, "2001",
                                    "No available IPs");

            sql << "UPDATE ip_pool SET status='RESERVED' WHERE ip=:ip",
                soci::use(ip);

            sql << "DELETE FROM ip_reservations WHERE ip=:ip",
                soci::use(ip);

            sql << "INSERT INTO ip_reservations(ip, service_id, reserved_at) "
                   "VALUES(:ip, :sid, NOW())",
                soci::use(ip), soci::use(serviceId);

            result.push_back(IpEntry(ip, t));
        }

        tr.commit();
    });

    return result;
}

// =========================
// ASSIGN IPS (re-checked inside the locked transaction)
// =========================
void Db::assignIps(const std::string& serviceId, const std::vector<std::string>& ips)
{
    soci::session sql(pool);
    executeWithRetry([&]() {
        soci::transaction tr(sql);
        lockInventory(sql);

        const std::string notExpired = "r.reserved_at > NOW() - " + ttlInterval(ttlSeconds);

        for (const std::string& ip : ips)
        {
            int count = 0;
            sql << ("SELECT COUNT(*) FROM ip_reservations r "
                    "JOIN ip_pool p ON p.ip = r.ip "
                    "WHERE r.ip=:ip AND r.service_id=:sid "
                    "AND p.status='RESERVED' AND " + notExpired),
                soci::use(ip), soci::use(serviceId), soci::into(count);

            if (count == 0)
            {
                if (!inPool(sql, ip))
                    throw BusinessError(BusinessError::Kind::NotFound, "3002",
                                        "IP not in pool: " + ip);

                throw BusinessError(BusinessError::Kind::Conflict, "3001",
                                    "IP is not reserved for this serviceId (or the reservation expired): " + ip);
            }

            sql << "UPDATE ip_pool SET status='ASSIGNED' WHERE ip=:ip",
                soci::use(ip);

            sql << "INSERT INTO ip_assignments(ip, service_id, assigned_at, active) "
                   "VALUES(:ip, :sid, NOW(), TRUE)",
                soci::use(ip), soci::use(serviceId);

            sql << "DELETE FROM ip_reservations WHERE ip=:ip",
                soci::use(ip);
        }

        tr.commit();
    });
}

// =========================
// TERMINATE IPS
// =========================
void Db::terminateIps(const std::string& serviceId, const std::vector<std::string>& ips)
{
    soci::session sql(pool);
    executeWithRetry([&]() {
        soci::transaction tr(sql);
        lockInventory(sql);

        for (const std::string& ip : ips)
        {
            int count = 0;
            sql << "SELECT COUNT(*) FROM ip_assignments "
                   "WHERE ip=:ip AND service_id=:sid AND active=TRUE",
                soci::use(ip), soci::use(serviceId), soci::into(count);

            if (count == 0)
            {
                if (!inPool(sql, ip))
                    throw BusinessError(BusinessError::Kind::NotFound, "3002",
                                        "IP not in pool: " + ip);

                throw BusinessError(BusinessError::Kind::Conflict, "4001",
                                    "IP is not assigned to this serviceId: " + ip);
            }

            sql << "UPDATE ip_assignments SET active=FALSE "
                   "WHERE ip=:ip AND service_id=:sid AND active=TRUE",
                soci::use(ip), soci::use(serviceId);

            sql << "UPDATE ip_pool SET status='FREE' WHERE ip=:ip",
                soci::use(ip);
        }

        tr.commit();
    });
}

// =========================
// CHANGE SERVICE ID
// =========================
void Db::changeServiceId(const std::string& oldId, const std::string& newId)
{
    soci::session sql(pool);
    executeWithRetry([&]() {
        soci::transaction tr(sql);
        lockInventory(sql);

        releaseExpiredLocked(sql, ttlSeconds);

        int assigned = 0;
        sql << "SELECT COUNT(*) FROM ip_assignments WHERE service_id=:sid AND active=TRUE",
            soci::use(oldId), soci::into(assigned);

        int reserved = 0;
        sql << "SELECT COUNT(*) FROM ip_reservations WHERE service_id=:sid",
            soci::use(oldId), soci::into(reserved);

        if (assigned == 0 && reserved == 0)
            throw BusinessError(BusinessError::Kind::NotFound, "6001",
                                "No IP is assigned or reserved for serviceIdOld");

        sql << "UPDATE ip_assignments SET service_id=:newid "
               "WHERE service_id=:oldid AND active=TRUE",
            soci::use(newId, "newid"), soci::use(oldId, "oldid");

        sql << "UPDATE ip_reservations SET service_id=:newid WHERE service_id=:oldid",
            soci::use(newId, "newid"), soci::use(oldId, "oldid");

        tr.commit();
    });
}

// =========================
// GET BY SERVICE
// =========================
std::vector<IpEntry>
Db::getByService(const std::string& serviceId)
{
    soci::session sql(pool);
    std::vector<IpEntry> result;

    soci::rowset<soci::row> rs =
        (sql.prepare <<
            "SELECT p.ip, p.ip_type "
            "FROM ip_pool p "
            "JOIN ip_assignments a ON a.ip=p.ip "
            "WHERE a.service_id=:sid AND a.active=TRUE "
            "ORDER BY p.ip",
            soci::use(serviceId));

    for (auto it = rs.begin(); it != rs.end(); ++it)
    {
        result.push_back(IpEntry(
            it->get<std::string>(0),
            it->get<std::string>(1)
        ));
    }

    return result;
}

// =========================
// RELEASE EXPIRED RESERVATIONS (cleaner job)
// =========================
void Db::releaseExpiredReservations()
{
    soci::session sql(pool);
    executeWithRetry([&]() {
        soci::transaction tr(sql);
        lockInventory(sql);

        releaseExpiredLocked(sql, ttlSeconds);

        tr.commit();
    });
}
