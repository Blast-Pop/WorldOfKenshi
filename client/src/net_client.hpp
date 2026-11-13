#pragma once
// Client networking on a dedicated thread. The game thread only pushes the local state and drains
// received messages; it never blocks on the network.

#include <core/config.hpp>
#include <wok/protocol.hpp>

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace wok {

class ClientNet {
public:
    enum class Status { Disconnected, Connecting, Connected, Rejected };

    void start(const ClientConfig& cfg);
    void stop();
    void pushLocal(const PlayerState& state);  // game thread
    std::vector<Message> drainIncoming();      // game thread
    Status status() const { return status_; }
    RejectReason rejectReason() const { return reject_; }
    PlayerId playerId() const { return playerId_; }

private:
    void run(ClientConfig cfg);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<Status> status_{Status::Disconnected};
    std::atomic<RejectReason> reject_{RejectReason::None};
    std::atomic<PlayerId> playerId_{0};
    std::mutex mutex_;
    std::optional<PlayerState> outgoing_;
    std::vector<Message> incoming_;
};

}  // namespace wok
