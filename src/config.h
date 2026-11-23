#pragma once

#include <cstdlib>
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
}
