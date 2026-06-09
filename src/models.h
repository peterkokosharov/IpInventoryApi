#pragma once

#include <string>
#include <vector>

// =========================
// IP STATE MACHINE
// =========================
enum class IpStatus
{
    FREE,
    RESERVED,
    ASSIGNED
};

// =========================
// IP TYPE
// =========================
enum class IpType
{
    IPv4,
    IPv6,
    BOTH
};

// =========================
// IP ENTITY
// =========================
struct IpRecord
{
    std::string ip;
    std::string ipType;
    IpStatus status;
};

// =========================
// REQUEST DTOs
// =========================

// /ip-pool
struct AddIpPoolRequest
{
    struct IpItem
    {
        std::string ip;
        std::string ipType;
    };

    std::vector<IpItem> ipAddresses;
};

// /reserve-ip
struct ReserveIpRequest
{
    std::string serviceId;
    std::string ipType;
};

// /assign-ip-serviceId
struct AssignIpRequest
{
    std::string serviceId;

    struct IpItem
    {
        std::string ip;
    };

    std::vector<IpItem> ipAddresses;
};

// /terminate-ip-serviceId
struct TerminateIpRequest
{
    std::string serviceId;

    struct IpItem
    {
        std::string ip;
    };

    std::vector<IpItem> ipAddresses;
};

// /serviceId-change
struct ServiceIdChangeRequest
{
    std::string serviceIdOld;
    std::string serviceId;
};

// =========================
// RESPONSE DTOs (optional internal use)
// =========================
struct IpResponseItem
{
    std::string ip;
    std::string ipType;
};