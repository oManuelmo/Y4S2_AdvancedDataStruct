#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

enum class BadUrlReason {
    None,
    Empty,
    TooLong,
    HasWhitespace,
    UnsupportedScheme,
    MissingHost
};

struct BadUrlCheckResult {
    bool bad;
    BadUrlReason reason;
};

inline std::string toLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

inline BadUrlCheckResult isBadUrl(const std::string& url) {
    if (url.empty())         return {true, BadUrlReason::Empty};
    if (url.size() > 2048)  return {true, BadUrlReason::TooLong};

    if (std::any_of(url.begin(), url.end(),
                    [](unsigned char c) { return std::isspace(c) != 0; }))
        return {true, BadUrlReason::HasWhitespace};

    const std::string lower = toLowerAscii(url);
    const bool isHttp  = lower.rfind("http://",  0) == 0;
    const bool isHttps = lower.rfind("https://", 0) == 0;
    if (!isHttp && !isHttps) return {true, BadUrlReason::UnsupportedScheme};

    const std::size_t schemeEnd = lower.find("://");
    if (schemeEnd == std::string::npos || schemeEnd + 3 >= lower.size())
        return {true, BadUrlReason::MissingHost};

    const std::size_t hostStart = schemeEnd + 3;
    const std::size_t hostEnd   = lower.find('/', hostStart);
    const std::string host      = lower.substr(hostStart, hostEnd - hostStart);
    if (host.empty()) return {true, BadUrlReason::MissingHost};

    return {false, BadUrlReason::None};
}

// ─────────────────────────────────────────────
//  URL Normalization
//
//  Canonical form:
//    1. Lowercase the scheme and host
//    2. Remove default ports  (:80 for http, :443 for https)
//    3. Remove the fragment  (#section)
//    4. Remove trailing slash on bare paths  (example.org/ → example.org)
//    5. Percent-decode unreserved characters  (RFC 3986 §2.3)
//    6. Upper-case percent-encoding hex digits  (%2f → %2F)
//
//  Returns std::nullopt if the URL is invalid.
// ─────────────────────────────────────────────

namespace detail {

inline bool isUnreserved(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

inline int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Normalize percent-encoding in a path/query component:
//  - decode %XX where XX is an unreserved character
//  - uppercase hex digits in remaining %XX sequences
inline std::string normalizePctEncoding(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hexVal(s[i + 1]);
            const int lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                const char decoded = static_cast<char>((hi << 4) | lo);
                if (isUnreserved(decoded)) {
                    out += decoded;
                } else {
                    out += '%';
                    out += static_cast<char>(std::toupper(static_cast<unsigned char>(s[i + 1])));
                    out += static_cast<char>(std::toupper(static_cast<unsigned char>(s[i + 2])));
                }
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

} // namespace detail

inline std::optional<std::string> normalizeUrl(const std::string& url) {
    if (isBadUrl(url).bad) return std::nullopt;

    // 1. Lowercase scheme
    std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return std::nullopt;

    std::string scheme = toLowerAscii(url.substr(0, schemeEnd));
    const bool isHttps = (scheme == "https");

    std::string rest = url.substr(schemeEnd + 3);  // everything after "://"

    // 2. Split off fragment
    const std::size_t fragPos = rest.find('#');
    if (fragPos != std::string::npos) rest = rest.substr(0, fragPos);

    // 3. Lowercase host (up to first '/' or '?' or ':')
    std::size_t pathStart = rest.find_first_of("/?");
    std::string authority = (pathStart == std::string::npos) ? rest : rest.substr(0, pathStart);
    std::string pathQuery  = (pathStart == std::string::npos) ? "" : rest.substr(pathStart);

    // Lowercase the authority (host[:port])
    authority = toLowerAscii(authority);

    // 4. Strip default ports
    const std::size_t colonPos = authority.rfind(':');
    if (colonPos != std::string::npos) {
        const std::string port = authority.substr(colonPos + 1);
        if ((isHttps && port == "443") || (!isHttps && port == "80")) {
            authority = authority.substr(0, colonPos);
        }
    }

    // 5. Normalize percent-encoding in the path+query
    pathQuery = detail::normalizePctEncoding(pathQuery);

    // 6. Remove trailing slash on bare paths  ("example.org/" → "example.org")
    if (pathQuery == "/") pathQuery.clear();

    return scheme + "://" + authority + pathQuery;
}
