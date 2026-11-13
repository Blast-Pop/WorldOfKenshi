#include <wok/openid.hpp>

#include <cctype>

namespace wok::openid {

namespace {

constexpr std::string_view kSteamLogin = "https://steamcommunity.com/openid/login";
constexpr std::string_view kSteamIdPrefix = "https://steamcommunity.com/openid/id/";
constexpr std::string_view kIdentifierSelect = "http://specs.openid.net/auth/2.0/identifier_select";
constexpr std::string_view kOpenIdNs = "http://specs.openid.net/auth/2.0";

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

std::string urlEncode(std::string_view s) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(digits[c >> 4]);
            out.push_back(digits[c & 15]);
        }
    }
    return out;
}

std::string urlDecode(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out.push_back(' ');
        } else if (s[i] == '%' && i + 2 < s.size() && hexValue(s[i + 1]) >= 0 && hexValue(s[i + 2]) >= 0) {
            out.push_back(static_cast<char>(hexValue(s[i + 1]) * 16 + hexValue(s[i + 2])));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

Params parseQuery(std::string_view query) {
    Params p;
    while (!query.empty()) {
        size_t amp = query.find('&');
        std::string_view pair = query.substr(0, amp);
        query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
        if (pair.empty()) continue;
        size_t eq = pair.find('=');
        if (eq == std::string_view::npos)
            p[urlDecode(pair)] = "";
        else
            p[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
    }
    return p;
}

std::string steamLoginUrl(const std::string& returnTo, const std::string& realm) {
    return std::string(kSteamLogin) + "?openid.ns=" + urlEncode(kOpenIdNs) + "&openid.mode=checkid_setup" +
           "&openid.return_to=" + urlEncode(returnTo) + "&openid.realm=" + urlEncode(realm) +
           "&openid.identity=" + urlEncode(kIdentifierSelect) + "&openid.claimed_id=" + urlEncode(kIdentifierSelect);
}

std::optional<uint64_t> steamIdFromClaimedId(std::string_view claimedId) {
    if (claimedId.substr(0, kSteamIdPrefix.size()) != kSteamIdPrefix) return std::nullopt;
    std::string_view digits = claimedId.substr(kSteamIdPrefix.size());
    if (digits.size() != 17) return std::nullopt;
    uint64_t v = 0;
    for (char c : digits) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    return v;
}

bool callbackLooksValid(const Params& p, std::string_view expectedReturnToPrefix) {
    auto get = [&](const char* k) -> std::string_view {
        auto it = p.find(k);
        return it == p.end() ? std::string_view{} : std::string_view(it->second);
    };
    return get("openid.mode") == "id_res" && get("openid.op_endpoint") == kSteamLogin &&
           get("openid.return_to").substr(0, expectedReturnToPrefix.size()) == expectedReturnToPrefix &&
           steamIdFromClaimedId(get("openid.claimed_id")).has_value();
}

std::string checkAuthenticationBody(const Params& callbackParams) {
    std::string body;
    for (auto& [k, v] : callbackParams) {
        if (k.rfind("openid.", 0) != 0) continue;
        if (!body.empty()) body += '&';
        body += urlEncode(k) + "=" + urlEncode(k == "openid.mode" ? std::string("check_authentication") : v);
    }
    return body;
}

bool isValidResponse(std::string_view body) {
    while (!body.empty()) {
        size_t nl = body.find('\n');
        std::string_view line = body.substr(0, nl);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line == "is_valid:true") return true;
        body = nl == std::string_view::npos ? std::string_view{} : body.substr(nl + 1);
    }
    return false;
}

}  // namespace wok::openid
