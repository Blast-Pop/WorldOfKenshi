#pragma once
// Steam OpenID 2.0 helpers (pure, no networking). Flow and rationale: docs/m2-auth.md.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace wok::openid {

using Params = std::map<std::string, std::string>;

std::string urlEncode(std::string_view s);
std::string urlDecode(std::string_view s);  // '+' -> space; malformed escapes kept as-is
Params parseQuery(std::string_view query);

// Steam login page the player's browser is sent to.
std::string steamLoginUrl(const std::string& returnTo, const std::string& realm);

// SteamID64 from "https://steamcommunity.com/openid/id/<17 digits>", nullopt otherwise.
std::optional<uint64_t> steamIdFromClaimedId(std::string_view claimedId);

// Cheap local checks before asking Steam: positive assertion, Steam endpoint, return_to is ours.
bool callbackLooksValid(const Params& p, std::string_view expectedReturnToPrefix);

// Body of the POST to Steam that confirms the assertion (only openid.* fields, mode switched).
std::string checkAuthenticationBody(const Params& callbackParams);

// True when Steam's key-value answer contains the line "is_valid:true".
bool isValidResponse(std::string_view body);

}  // namespace wok::openid
