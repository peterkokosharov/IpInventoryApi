// Unit test for src/ip_utils (plain checks, no framework).
#include "ip_utils.h"

#include <iostream>
#include <string>

static int failures = 0;
static int total = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        total++;                                                             \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            std::cerr << "FAIL line " << __LINE__ << ": " #cond << std::endl; \
        }                                                                    \
    } while (0)

static bool parsesTo(const std::string& in, const std::string& wantCanon, const std::string& wantType)
{
    std::string canon, type;
    return ip_utils::parse(in, canon, type) && canon == wantCanon && type == wantType;
}

static bool rejects(const std::string& in)
{
    std::string canon, type;
    return !ip_utils::parse(in, canon, type);
}

int main()
{
    // IPv4
    CHECK(parsesTo("95.44.73.19", "95.44.73.19", "IPv4"));
    CHECK(parsesTo("0.0.0.0", "0.0.0.0", "IPv4"));
    CHECK(parsesTo("255.255.255.255", "255.255.255.255", "IPv4"));
    CHECK(rejects("256.1.1.1"));
    CHECK(rejects("999.1.1.1"));
    CHECK(rejects("1.2.3"));
    CHECK(rejects("1.2.3.4.5"));
    CHECK(rejects("1.2.3.a"));

    // IPv6: full form (example from the task) stays the same
    CHECK(parsesTo("2a01:05a9:01a4:095c:0000:0000:0000:0001",
                   "2a01:05a9:01a4:095c:0000:0000:0000:0001", "IPv6"));
    // short groups are padded, upper case becomes lower case
    CHECK(parsesTo("2001:db8:0:0:0:0:0:1", "2001:0db8:0000:0000:0000:0000:0000:0001", "IPv6"));
    CHECK(parsesTo("2001:DB8:0:0:0:0:0:1", "2001:0db8:0000:0000:0000:0000:0000:0001", "IPv6"));
    // compressed forms are expanded
    CHECK(parsesTo("2001:db8::1", "2001:0db8:0000:0000:0000:0000:0000:0001", "IPv6"));
    CHECK(parsesTo("::1", "0000:0000:0000:0000:0000:0000:0000:0001", "IPv6"));
    CHECK(parsesTo("::", "0000:0000:0000:0000:0000:0000:0000:0000", "IPv6"));
    // two spellings of the same address give the same canonical text
    {
        std::string a, b, t;
        CHECK(ip_utils::parse("2a01:5a9:1a4:95c::1", a, t));
        CHECK(ip_utils::parse("2a01:05a9:01a4:095c:0000:0000:0000:0001", b, t));
        CHECK(a == b);
    }
    CHECK(rejects("2001:db8::1::2"));
    CHECK(rejects("2001:db8:0:0:0:0:0:0:1"));
    CHECK(rejects("2001:db8:zz::1"));
    CHECK(rejects("12345::1"));

    // garbage
    CHECK(rejects(""));
    CHECK(rejects(" 1.2.3.4"));
    CHECK(rejects("1.2.3.4 "));
    CHECK(rejects("hello"));
    CHECK(rejects("1.2.3.4/24"));
    CHECK(rejects(std::string("1.2.3.4\0x", 9)));

    // ip types
    CHECK(ip_utils::isValidIpType("IPv4"));
    CHECK(ip_utils::isValidIpType("IPv6"));
    CHECK(!ip_utils::isValidIpType("Both"));
    CHECK(!ip_utils::isValidIpType("ipv4"));
    CHECK(!ip_utils::isValidIpType(""));

    std::cout << (total - failures) << "/" << total << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
