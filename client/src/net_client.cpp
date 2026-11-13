#include "net_client.hpp"

#include "log.hpp"

#include <wok/transport.hpp>

#include <chrono>
#include <cstring>

namespace wok {

void ClientNet::start(const ClientConfig& cfg) {
    stop();
    running_ = true;
    status_ = Status::Connecting;
    reject_ = RejectReason::None;
    playerId_ = 0;
    thread_ = std::thread([this, cfg] { run(cfg); });
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

std::vector<Message> ClientNet::drainIncoming() {
    std::lock_guard lock(mutex_);
    return std::exchange(incoming_, {});
}

void ClientNet::run(ClientConfig cfg) {
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
                    h.steamId = 0;  // Steam ticket auth comes after M1
                    std::strncpy(h.name, cfg.name.c_str(), sizeof(h.name) - 1);
                    auto b = encode(h);
                    net.send(b, true);
                    log("net: connected, Hello sent as %s", h.name);
                } else if (ev.kind == NetEvent::Disconnected) {
                    log("net: disconnected");
                    if (status_ != Status::Rejected) status_ = Status::Disconnected;
                    running_ = false;
                } else if (auto m = decode(ev.data)) {
                    if (auto* ack = std::get_if<HelloAck>(&*m)) {
                        if (ack->reason == RejectReason::None) {
                            playerId_ = ack->playerId;
                            status_ = Status::Connected;
                            log("net: joined as player %u", ack->playerId);
                        } else {
                            reject_ = ack->reason;
                            status_ = Status::Rejected;
                            log("net: rejected (reason %u)", static_cast<unsigned>(ack->reason));
                        }
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
            if (status_ == Status::Connected) {
                std::optional<PlayerState> out;
                {
                    std::lock_guard lock(mutex_);
                    out = std::exchange(outgoing_, std::nullopt);
                }
                if (out) {
                    auto b = encode(*out);
                    net.send(b, false);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        net.disconnect();
    }
    netShutdown();
}

}  // namespace wok
