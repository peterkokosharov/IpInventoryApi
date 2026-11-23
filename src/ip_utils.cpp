#include "ip_utils.h"

#include <cstdio>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace ip_utils
{
    bool parse(const std::string& text, std::string& canonical, std::string& type)
    {
        if (text.empty() || text.size() > 45)
            return false;
        if (text.find('\0') != std::string::npos)
            return false;

        unsigned char buf[16] = {0};

        if (inet_pton(AF_INET, text.c_str(), buf) == 1)
        {
            char out[32];
            std::snprintf(out, sizeof(out), "%u.%u.%u.%u",
                          static_cast<unsigned>(buf[0]), static_cast<unsigned>(buf[1]),
                          static_cast<unsigned>(buf[2]), static_cast<unsigned>(buf[3]));
            canonical = out;
            type = "IPv4";
            return true;
        }

        if (inet_pton(AF_INET6, text.c_str(), buf) == 1)
        {
            char out[64];
            std::snprintf(out, sizeof(out),
                          "%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
                          "%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                          static_cast<unsigned>(buf[0]), static_cast<unsigned>(buf[1]),
                          static_cast<unsigned>(buf[2]), static_cast<unsigned>(buf[3]),
                          static_cast<unsigned>(buf[4]), static_cast<unsigned>(buf[5]),
                          static_cast<unsigned>(buf[6]), static_cast<unsigned>(buf[7]),
                          static_cast<unsigned>(buf[8]), static_cast<unsigned>(buf[9]),
                          static_cast<unsigned>(buf[10]), static_cast<unsigned>(buf[11]),
                          static_cast<unsigned>(buf[12]), static_cast<unsigned>(buf[13]),
                          static_cast<unsigned>(buf[14]), static_cast<unsigned>(buf[15]));
            canonical = out;
            type = "IPv6";
            return true;
        }

        return false;
    }

    bool isValidIpType(const std::string& type)
    {
        return type == "IPv4" || type == "IPv6";
    }
}
