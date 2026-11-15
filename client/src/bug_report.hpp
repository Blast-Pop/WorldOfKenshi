#pragma once
// "Report a bug": sends the player's description with the mod's and Kenshi's logs (and a recent
// crash dump) to the community bot, which posts it in the Discord #bug-reports forum. Runs on its
// own thread; the window reads status().

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace wok {

struct BugReportInfo {
    std::string title, description;
    std::string version, server, character;
    std::string modDir;  // Kenshi\WorldOfKenshi: our logs; its parent holds Kenshi's
};

class BugReporter {
public:
    ~BugReporter();
    bool busy() const { return busy_; }
    void send(const std::string& url, BugReportInfo info);
    std::string status() const;  // empty, "Sending...", the result or the error

private:
    void setStatus(std::string s);
    std::thread thread_;
    std::atomic<bool> busy_{false};
    mutable std::mutex mutex_;
    std::string status_;
};

}  // namespace wok
