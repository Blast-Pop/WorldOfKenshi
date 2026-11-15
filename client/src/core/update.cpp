#include "update.hpp"

#include <cctype>

namespace wok {

namespace {

// The string value of `"key"` at or after `from`, before `end`; npos-safe. No escapes are expected in
// the values read here (tags, file names, URLs, digests).
std::optional<std::string> stringAfter(std::string_view json, std::string_view key, size_t from, size_t end,
                                       size_t* at = nullptr) {
    const std::string quoted = "\"" + std::string(key) + "\"";
    size_t p = json.find(quoted, from);
    if (p == std::string_view::npos || p >= end) return std::nullopt;
    if (at) *at = p;
    p += quoted.size();
    while (p < end && (json[p] == ' ' || json[p] == ':')) ++p;
    if (p >= end || json[p] != '"') return std::nullopt;
    const size_t close = json.find('"', p + 1);
    if (close == std::string_view::npos || close > end) return std::nullopt;
    return std::string(json.substr(p + 1, close - p - 1));
}

bool safeUrl(std::string_view url) {
    if (!url.starts_with(kDownloadPrefix)) return false;
    for (char c : url)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == ':' || c == '.' || c == '-' || c == '_'))
            return false;
    return true;
}

}  // namespace

std::optional<std::array<int, 3>> parseVersion(std::string_view tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.remove_prefix(1);
    std::array<int, 3> v{};
    size_t p = 0;
    for (int part = 0; part < 3; ++part) {
        if (p >= tag.size() || !std::isdigit(static_cast<unsigned char>(tag[p]))) return std::nullopt;
        int n = 0;
        while (p < tag.size() && std::isdigit(static_cast<unsigned char>(tag[p]))) {
            if (n > 100000) return std::nullopt;
            n = n * 10 + (tag[p++] - '0');
        }
        v[static_cast<size_t>(part)] = n;
        if (part < 2) {
            if (p >= tag.size() || tag[p] != '.') return std::nullopt;
            ++p;
        }
    }
    return v;
}

bool isNewer(std::string_view candidate, std::string_view current) {
    const auto a = parseVersion(candidate), b = parseVersion(current);
    return a && b && *a > *b;
}

std::optional<UpdateInfo> findUpdate(std::string_view json, std::string_view current) {
    std::optional<UpdateInfo> best;
    size_t pos = json.find("\"tag_name\"");
    while (pos != std::string_view::npos) {
        size_t next = json.find("\"tag_name\"", pos + 1);
        const size_t end = next == std::string_view::npos ? json.size() : next;
        const auto tag = stringAfter(json, "tag_name", pos, end);
        const auto version = tag ? parseVersion(*tag) : std::nullopt;
        if (version && isNewer(*tag, best ? std::string_view(best->version) : current)) {
            // The asset object of the client DLL: its name, then (later in the same object) its
            // digest and download address.
            size_t from = pos;
            for (;;) {
                size_t at = 0;
                const auto name = stringAfter(json, "name", from, end, &at);
                if (!name) break;
                from = at + 1;
                if (*name != kUpdateAsset) continue;
                const auto url = stringAfter(json, "browser_download_url", at, end);
                if (!url || !safeUrl(*url) || !url->ends_with(kUpdateAsset)) break;
                UpdateInfo info;
                info.version = std::to_string((*version)[0]) + "." + std::to_string((*version)[1]) + "." +
                               std::to_string((*version)[2]);
                info.url = *url;
                size_t digestAt = 0;
                if (auto digest = stringAfter(json, "digest", at, end, &digestAt);
                    digest && digest->starts_with("sha256:") && digest->size() == 7 + 64 &&
                    digestAt < json.find(*url, at))
                    info.sha256 = digest->substr(7);
                best = std::move(info);
                break;
            }
        }
        pos = next;
    }
    return best;
}

}  // namespace wok
