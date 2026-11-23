#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>

// Runtime configuration from environment variables.
namespace config
{
    inline std::string env(const char* name, const std::string& fallback)
    {
        const char* v = std::getenv(name);
        return (v && *v) ? std::string(v) : fallback;
    }

    inline std::string dbConn()
    {
        return env("DB_CONN",
                   "dbname=ipinventory user=ipinventory password=ipinventory "
                   "host=localhost port=5432");
    }

    inline std::string staticDir()
    {
        return env("STATIC_DIR", "./static");
    }

    inline int port()
    {
        try
        {
            return std::stoi(env("PORT", "18080"));
        }
        catch (const std::exception&)
        {
            return 18080;
        }
    }

    inline int positiveInt(const char* name, int fallback)
    {
        try
        {
            const int v = std::stoi(env(name, std::to_string(fallback)));
            return v > 0 ? v : fallback;
        }
        catch (const std::exception&)
        {
            return fallback;
        }
    }

    // A reservation that is not assigned within this time expires.
    inline int reservationTtlSeconds()
    {
        return positiveInt("RESERVATION_TTL_SECONDS", 900);
    }

    // How often the background job releases expired reservations.
    inline int cleanupIntervalSeconds()
    {
        return positiveInt("CLEANUP_INTERVAL_SECONDS", 60);
    }
}
