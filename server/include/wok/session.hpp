#pragma once
// Pure session logic of the dedicated server: no sockets, fully unit-testable.
// The transport feeds events in; the manager returns the messages to send.

#include <wok/protocol.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wok {

struct Outgoing {
    ConnId to;
    Message msg;
};

struct SessionConfig {
    uint32_t maxPlayers = 250;
    double timeoutSec = 10.0;
    uint32_t maxBadPackets = 3;
    std::unordered_set<uint64_t> bannedSteamIds;
};

class SessionManager {
public:
    explicit SessionManager(SessionConfig cfg);

    std::vector<Outgoing> onConnect(ConnId conn, double nowSec = 0.0);
    std::vector<Outgoing> onPacket(ConnId conn, std::span<const uint8_t> bytes, double nowSec);
    std::vector<Outgoing> onDisconnect(ConnId conn);
    std::vector<Outgoing> tick(double nowSec);  // handles timeouts

    // Connections the transport must close; cleared by each call.
    std::vector<ConnId> connectionsToClose();

    size_t playerCount() const;

private:
    struct Conn {
        double lastSeen = 0.0;
        uint32_t badPackets = 0;
        std::optional<PlayerId> player;  // set once Hello is accepted
        std::optional<PlayerState> lastState;
    };

    std::vector<Outgoing> handleHello(ConnId conn, Conn& c, const Hello& h);
    std::vector<Outgoing> handleState(ConnId conn, Conn& c, PlayerState s);
    std::vector<Outgoing> dropConn(ConnId conn);  // removes and broadcasts PlayerLeft if needed
    void requestClose(ConnId conn);

    SessionConfig cfg_;
    std::unordered_map<ConnId, Conn> conns_;
    std::vector<ConnId> toClose_;
    PlayerId nextPlayerId_ = 1;
};

}  // namespace wok
