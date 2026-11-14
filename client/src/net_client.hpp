#pragma once
// Client networking on a dedicated thread. The game thread only pushes the local state and drains
// received messages; it never blocks on the network.

#include <core/config.hpp>
#include <wok/protocol.hpp>

#include <atomic>
#include <mutex>
#include <map>
#include <optional>
#include <thread>
#include <vector>

namespace wok {

class ClientNet {
public:
    enum class Status { Disconnected, Connecting, AwaitingSteamLogin, Connected, Rejected };

    // `token`: the secret this server gave us earlier (zero on a first visit).
    // `ticket`: Steam session ticket (empty if Steam is unavailable), checked by the server with Steam.
    void start(const ClientConfig& cfg, uint64_t steamId, const AuthToken& token, std::string ticket = {});
    void stop();
    void pushLocal(const PlayerState& state);         // game thread
    void createCharacter(const CreateCharacter& c);   // game thread
    void sendStats(const StatsUpdate& u);             // game thread; merged until sent (reliable)
    void sendInventory(std::vector<uint8_t> items);   // game thread; the latest one is sent (reliable)
    std::vector<Message> drainIncoming();             // game thread
    std::optional<AuthToken> takeIssuedToken();       // new token to persist, once
    std::optional<CharacterData> character() const;   // our server-side character, once known
    Status status() const { return status_; }
    RejectReason rejectReason() const { return reject_; }
    PlayerId playerId() const { return playerId_; }
    bool hasCharacter() const { return hasCharacter_; }

private:
    void run(ClientConfig cfg, uint64_t steamId, AuthToken token, std::string ticket);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<Status> status_{Status::Disconnected};
    std::atomic<RejectReason> reject_{RejectReason::None};
    std::atomic<PlayerId> playerId_{0};
    std::atomic<bool> hasCharacter_{false};
    mutable std::mutex mutex_;
    std::optional<PlayerState> outgoing_;
    std::optional<CreateCharacter> outgoingCreate_;
    std::map<uint8_t, float> outgoingStats_;
    std::optional<std::vector<uint8_t>> outgoingInventory_;
    std::vector<Message> incoming_;
    std::optional<AuthToken> issuedToken_;
    std::optional<CharacterData> character_;
};

}  // namespace wok
