#pragma once
// Thin facade over Valve GameNetworkingSockets (UDP, reliable + unreliable messages).
// All objects must be used from a single thread; poll() runs the library callbacks.

#include <wok/protocol.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace wok {

// Reference-counted global init of the library. Call once per user, pair with netShutdown().
bool netInit();
void netShutdown();

struct NetEvent {
    enum Kind { Connected, Disconnected, Packet } kind;
    ConnId conn = 0;
    std::vector<uint8_t> data;  // Packet only
};

class NetServer {
public:
    NetServer() = default;
    ~NetServer();
    NetServer(const NetServer&) = delete;
    NetServer& operator=(const NetServer&) = delete;

    bool listen(uint16_t port);
    void send(ConnId conn, std::span<const uint8_t> data, bool reliable);
    void close(ConnId conn);
    std::vector<NetEvent> poll();

    // Internal: called by the library status callback.
    void onStatus(const void* info);

private:
    uint32_t listen_ = 0;
    uint32_t pollGroup_ = 0;
    std::vector<NetEvent> pending_;
};

class NetClient {
public:
    NetClient() = default;
    ~NetClient();
    NetClient(const NetClient&) = delete;
    NetClient& operator=(const NetClient&) = delete;

    // `host` must be an IPv4/IPv6 address or a resolvable host name.
    bool connect(const char* host, uint16_t port);
    void send(std::span<const uint8_t> data, bool reliable);
    std::vector<NetEvent> poll();
    bool connected() const { return connected_; }
    void disconnect();

    // Internal: called by the library status callback.
    void onStatus(const void* info);

private:
    uint32_t conn_ = 0;
    bool connected_ = false;
    std::vector<NetEvent> pending_;
};

}  // namespace wok
