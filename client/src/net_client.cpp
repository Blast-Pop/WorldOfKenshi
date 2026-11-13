#include "net_client.hpp"

#include "log.hpp"

#include <wok/transport.hpp>

#include <windows.h>

#include <shellapi.h>

#include <chrono>
#include <cstring>

namespace wok {

namespace {

// Only plain web links from the server are opened, never anything else (file://, programs...).
void openInBrowser(const std::string& url) {
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        log("net: refusing to open non-web URL from server");
        return;
    }
    // The server reuses the same login link while a Steam login is in progress: open it only once.
    static std::string lastUrl;
    static auto lastTime = std::chrono::steady_clock::time_point{};
    auto now = std::chrono::steady_clock::now();
    if (url == lastUrl && now - lastTime < std::chrono::minutes(5)) {
        log("net: Steam login page already open in the browser");
        return;
    }
    lastUrl = url;
    lastTime = now;
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace

void ClientNet::start(const ClientConfig& cfg, uint64_t steamId, const AuthToken& token) {
    stop();
    running_ = true;
    status_ = Status::Connecting;
    reject_ = RejectReason::None;
    playerId_ = 0;
    hasCharacter_ = false;
    {
        std::lock_guard lock(mutex_);
        character_.reset();
        issuedToken_.reset();
        outgoingCreate_.reset();
    }
    thread_ = std::thread([this, cfg, steamId, token] { run(cfg, steamId, token); });
}

void ClientNet::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    status_ = Status::Disconnected;
}

void ClientNet::pushLocal(const PlayerState& state) {
    std::lock_guard lock(mutex_);
    outgoing_ = state;
}

void ClientNet::createCharacter(const CreateCharacter& c) {
    std::lock_guard lock(mutex_);
    outgoingCreate_ = c;
}

std::vector<Message> ClientNet::drainIncoming() {
    std::lock_guard lock(mutex_);
    return std::exchange(incoming_, {});
}

std::optional<AuthToken> ClientNet::takeIssuedToken() {
    std::lock_guard lock(mutex_);
    return std::exchange(issuedToken_, std::nullopt);
}

std::optional<CharacterData> ClientNet::character() const {
    std::lock_guard lock(mutex_);
    return character_;
}

void ClientNet::run(ClientConfig cfg, uint64_t steamId, AuthToken token) {
    if (!netInit()) {
        log("net: GameNetworkingSockets init failed");
        status_ = Status::Disconnected;
        return;
    }
    {
        NetClient net;
        if (!net.connect(cfg.host.c_str(), cfg.port)) {
            log("net: cannot resolve %s", cfg.host.c_str());
            status_ = Status::Disconnected;
            running_ = false;
        } else {
            log("net: connecting to %s:%u", cfg.host.c_str(), cfg.port);
        }
        auto nextPing = std::chrono::steady_clock::now();
        while (running_) {
            for (auto& ev : net.poll()) {
                if (ev.kind == NetEvent::Connected) {
                    Hello h{};
                    h.steamId = steamId;
                    h.token = token;
                    std::strncpy(h.name, cfg.name.c_str(), sizeof(h.name) - 1);
                    auto b = encode(h);
                    net.send(b, true);
                    log("net: connected, Hello sent (%s)", token == AuthToken{} ? "first visit" : "with token");
                } else if (ev.kind == NetEvent::Disconnected) {
                    log("net: disconnected");
                    if (status_ != Status::Rejected) status_ = Status::Disconnected;
                    running_ = false;
                } else if (auto m = decode(ev.data)) {
                    if (auto* ch = std::get_if<AuthChallenge>(&*m)) {
                        status_ = Status::AwaitingSteamLogin;
                        log("net: Steam login required, opening the browser");
                        openInBrowser(ch->url);
                        continue;
                    }
                    if (auto* ack = std::get_if<HelloAck>(&*m)) {
                        if (ack->reason == RejectReason::None) {
                            playerId_ = ack->playerId;
                            hasCharacter_ = ack->hasCharacter;
                            if (ack->token != AuthToken{}) {
                                std::lock_guard lock(mutex_);
                                issuedToken_ = ack->token;
                            }
                            status_ = Status::Connected;
                            log("net: joined as player %u (%s)", ack->playerId,
                                ack->hasCharacter ? "has a character" : "no character yet");
                        } else {
                            reject_ = ack->reason;
                            status_ = Status::Rejected;
                            log("net: rejected (reason %u)", static_cast<unsigned>(ack->reason));
                        }
                        continue;
                    }
                    if (auto* cd = std::get_if<CharacterData>(&*m)) {
                        std::lock_guard lock(mutex_);
                        character_ = *cd;
                        hasCharacter_ = true;
                        log("net: character '%s' (%s) at (%.1f, %.1f, %.1f)", cd->name.c_str(), cd->race.c_str(), cd->x,
                            cd->y, cd->z);
                        continue;
                    }
                    std::lock_guard lock(mutex_);
                    incoming_.push_back(std::move(*m));
                }
            }
            if (status_ == Status::Connected && std::chrono::steady_clock::now() >= nextPing) {
                nextPing = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                auto b = encode(Ping{});
                net.send(b, false);
            }
            if (status_ == Status::AwaitingSteamLogin && std::chrono::steady_clock::now() >= nextPing) {
                nextPing = std::chrono::steady_clock::now() + std::chrono::seconds(1);  // stay alive while the
                auto b = encode(Ping{});                                                   // player logs in
                net.send(b, false);
            }
            if (status_ == Status::Connected) {
                std::optional<PlayerState> out;
                std::optional<CreateCharacter> create;
                {
                    std::lock_guard lock(mutex_);
                    out = std::exchange(outgoing_, std::nullopt);
                    create = std::exchange(outgoingCreate_, std::nullopt);
                }
                if (out) {
                    auto b = encode(*out);
                    net.send(b, false);
                }
                if (create) {
                    auto b = encode(*create);
                    net.send(b, true);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        net.disconnect();
    }
    netShutdown();
}

}  // namespace wok
