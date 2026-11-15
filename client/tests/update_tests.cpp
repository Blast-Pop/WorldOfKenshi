#include <doctest/doctest.h>

#include <core/update.hpp>

#include <string>

using namespace wok;

namespace {
const std::string kDigest(64, 'a');
std::string release(const std::string& tag, bool withDll) {
    std::string s = R"({"url":"https://api.github.com/x","author":{"login":"Blast-Pop"},"tag_name":")" + tag +
                    R"(","name":"World of Kenshi","draft":false,"prerelease":true,"assets":[)";
    s += R"({"name":"WorldOfKenshi-alpha.zip","digest":"sha256:)" + std::string(64, 'b') +
         R"(","browser_download_url":"https://github.com/Blast-Pop/WorldOfKenshi/releases/download/)" + tag +
         R"(/WorldOfKenshi-alpha.zip"})";
    if (withDll)
        s += R"(,{"name":"wok_client.dll","uploader":{"login":"Blast-Pop"},"size":3000000,"digest":"sha256:)" + kDigest +
             R"(","browser_download_url":"https://github.com/Blast-Pop/WorldOfKenshi/releases/download/)" + tag +
             R"(/wok_client.dll"})";
    return s + R"(],"body":"notes with \"tag_name\" inside"})";
}
}  // namespace

TEST_CASE("update_versions_compare_by_numbers") {
    CHECK(parseVersion("v0.3.1-alpha") == std::optional<std::array<int, 3>>({0, 3, 1}));
    CHECK(!parseVersion("alpha"));
    CHECK(!parseVersion("v1.2"));
    CHECK(isNewer("v0.10.0-alpha", "0.9.5"));
    CHECK(!isNewer("v0.3.1-alpha", "0.3.1"));
    CHECK(!isNewer("v0.3.0-alpha", "0.3.1"));
    CHECK(!isNewer("garbage", "0.3.1"));
}

TEST_CASE("update_found_in_the_newest_release_with_the_dll") {
    const std::string json = "[" + release("v0.3.2-alpha", true) + "," + release("v0.3.1-alpha", true) + "]";
    auto info = findUpdate(json, "0.3.1");
    REQUIRE(info);
    CHECK(info->version == "0.3.2");
    CHECK(info->url == "https://github.com/Blast-Pop/WorldOfKenshi/releases/download/v0.3.2-alpha/wok_client.dll");
    CHECK(info->sha256 == kDigest);
    CHECK(!findUpdate(json, "0.3.2"));  // up to date
    // A release without the DLL is skipped, an older one with it is not an update.
    CHECK(!findUpdate("[" + release("v0.4.0-alpha", false) + "," + release("v0.3.1-alpha", true) + "]", "0.3.1"));
    CHECK(!findUpdate("not json at all", "0.3.1"));
}

TEST_CASE("update_only_downloads_from_the_projects_releases") {
    std::string json = "[" + release("v0.3.2-alpha", true) + "]";
    const std::string good = "https://github.com/Blast-Pop/WorldOfKenshi/releases/download/v0.3.2-alpha/wok_client.dll";
    json.replace(json.find(good), good.size(), "https://evil.example/releases/download/v0.3.2-alpha/wok_client.dll");
    CHECK(!findUpdate(json, "0.3.1"));
}
