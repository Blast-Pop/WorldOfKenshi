#include <wok/auth_http.hpp>
#include <wok/openid.hpp>

#include <httplib.h>

#include <cstdio>

namespace wok {

namespace {

constexpr const char* kPageOk =
    "<!doctype html><meta charset=utf-8><title>World of Kenshi</title>"
    "<body style='font-family:sans-serif;background:#2b2620;color:#e8dcc0;text-align:center;padding-top:15%'>"
    "<h1>World of Kenshi</h1><p>Compte Steam vérifié. Tu peux retourner dans Kenshi.</p>"
    "<p>Steam account verified. You can go back to Kenshi.</p></body>";
constexpr const char* kPageFail =
    "<!doctype html><meta charset=utf-8><title>World of Kenshi</title>"
    "<body style='font-family:sans-serif;background:#2b2620;color:#e8dcc0;text-align:center;padding-top:15%'>"
    "<h1>World of Kenshi</h1><p>La vérification Steam a échoué. Réessaie depuis Kenshi.</p>"
    "<p>Steam verification failed. Please try again from Kenshi.</p></body>";

bool isNonce(const std::string& n) {
    if (n.size() != 64) return false;
    for (char c : n)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

}  // namespace

AuthHttp::AuthHttp(uint16_t port, std::string publicUrl)
    : port_(port), publicUrl_(std::move(publicUrl)), server_(std::make_unique<httplib::Server>()) {}

AuthHttp::~AuthHttp() {
    server_->stop();
    if (thread_.joinable()) thread_.join();
}

std::string AuthHttp::challengeUrlBase() const { return publicUrl_ + "/auth/start?nonce="; }

bool AuthHttp::start() {
    const std::string callback = publicUrl_ + "/auth/callback";

    server_->Get("/auth/start", [this, callback](const httplib::Request& req, httplib::Response& res) {
        std::string nonce = req.get_param_value("nonce");
        if (!isNonce(nonce)) {
            res.status = 400;
            res.set_content(kPageFail, "text/html; charset=utf-8");
            return;
        }
        res.set_redirect(openid::steamLoginUrl(callback + "?nonce=" + nonce, publicUrl_ + "/"));
    });

    server_->Get("/auth/callback", [this, callback](const httplib::Request& req, httplib::Response& res) {
        openid::Params p;
        for (auto& [k, v] : req.params) p[k] = v;
        std::string nonce = p.count("nonce") ? p["nonce"] : "";
        bool ok = isNonce(nonce) && openid::callbackLooksValid(p, callback);
        uint64_t steamId = 0;
        if (ok) {
            // Ask Steam to confirm the signed assertion (no API key needed).
            httplib::SSLClient steam("steamcommunity.com", 443);
            steam.set_connection_timeout(10);
            auto r = steam.Post("/openid/login", openid::checkAuthenticationBody(p), "application/x-www-form-urlencoded");
            ok = r && r->status == 200 && openid::isValidResponse(r->body);
            if (ok) steamId = *openid::steamIdFromClaimedId(p["openid.claimed_id"]);
        }
        if (ok) {
            std::lock_guard lock(mutex_);
            verified_.emplace_back(nonce, steamId);
        }
        std::printf("[auth] %s steamId=%llu\n", ok ? "verified" : "FAILED", static_cast<unsigned long long>(steamId));
        std::fflush(stdout);
        res.set_content(ok ? kPageOk : kPageFail, "text/html; charset=utf-8");
    });

    if (!server_->bind_to_port("0.0.0.0", port_)) return false;
    thread_ = std::thread([this] { server_->listen_after_bind(); });
    return true;
}

std::vector<std::pair<std::string, uint64_t>> AuthHttp::poll() {
    std::lock_guard lock(mutex_);
    return std::exchange(verified_, {});
}

}  // namespace wok
