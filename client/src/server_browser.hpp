#pragma once
// Server browser: on its own thread, asks every listed server for its ServerInfo every few seconds
// and measures the round trip (ping). The UI reads snapshots of the results.

#include <core/config.hpp>
#include <wok/protocol.hpp>

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace wok {

struct ServerRow {
    ServerEntry entry;
    enum class State { Querying, Online, Offline } state = State::Querying;
    std::optional<ServerInfo> info;
    int pingMs = -1;
};

class ServerBrowser {
public:
    ~ServerBrowser();
    void start(std::vector<ServerEntry> servers);  // (re)starts querying this list
    void stop();
    std::vector<ServerRow> rows() const;           // snapshot, same order as the list

private:
    void run();

    std::thread thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::vector<ServerRow> rows_;
};

}  // namespace wok
