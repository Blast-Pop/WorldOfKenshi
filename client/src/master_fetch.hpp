#pragma once
// Downloads the master server list ("<masterUrl>/master/servers") on its own thread, so the
// Multiplayer window can show every announced server without waiting.

#include <core/config.hpp>

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace wok {

class MasterFetch {
public:
    ~MasterFetch();
    void start(const std::string& masterUrl);  // no-op while a download is running
    // The last downloaded list (empty until the first answer, or if the master is unreachable).
    std::vector<ServerEntry> servers() const;

private:
    std::thread thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::vector<ServerEntry> servers_;
};

// Blocking HTTP GET (WinHTTP, short timeouts). Empty optional on any failure or non-200 answer.
std::optional<std::string> httpGet(const std::string& url);

}  // namespace wok
