#pragma once

#include <soci/soci.h>
#include <string>
#include <vector>

class Db
{
public:
    Db();

    // =========================
    // IP POOL
    // =========================
    void addIp(const std::string& ip, const std::string& type);

    // =========================
    // RESERVATION
    // =========================
    std::vector<std::pair<std::string, std::string>>
    reserveIp(const std::string& serviceId, const std::string& type);

    bool isReserved(const std::string& ip, const std::string& serviceId);

    // =========================
    // ASSIGNMENT
    // =========================
    void assignIp(const std::string& ip, const std::string& serviceId);

    bool isAssigned(const std::string& ip, const std::string& serviceId);

    void terminateIp(const std::string& ip, const std::string& serviceId);

    // =========================
    // QUERY
    // =========================
    std::vector<std::pair<std::string, std::string>>
    getByService(const std::string& serviceId);

    // =========================
    // CLEANUP JOB
    // =========================
    void releaseExpiredReservations();

private:
    soci::session sql;
};