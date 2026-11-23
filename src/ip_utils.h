#pragma once

#include <string>

// Pure helpers (no Crow / SOCI dependency) so they can be unit tested.
namespace ip_utils
{
    // Parses an IPv4 or IPv6 address (compressed IPv6 such as "2001:db8::1"
    // is accepted). On success returns true, sets `canonical` and `type`
    // ("IPv4" or "IPv6").
    //
    // Canonical forms:
    //   IPv4: dotted decimal, e.g. "95.44.73.19"
    //   IPv6: eight groups of four lowercase hex digits,
    //         e.g. "2a01:05a9:01a4:095c:0000:0000:0000:0001"
    //         (the same shape as the examples in the task description)
    bool parse(const std::string& text, std::string& canonical, std::string& type);

    // True for exactly "IPv4" or "IPv6".
    bool isValidIpType(const std::string& type);
}
