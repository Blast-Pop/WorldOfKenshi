#include "server_browser.hpp"

#include "log.hpp"

#include <wok/transport.hpp>

#include <chrono>
#include <memory>

namespace wok {

namespace {
using Clock = std::chrono::steady_clock;
constexpr auto kQueryTimeout = std::chrono::seconds(3);
constexpr auto kRefreshEvery = std::chrono::seconds(5);
}  // namespace

ServerBrowser::~ServerBrowser() { stop(); }

void ServerBrowser::start(std::vector<ServerEntry> servers) {
    stop();
    {
        std::lock_guard lock(mutex_);
        rows_.clear();
        for (auto& e : servers) rows_.push_back(ServerRow{e});
    }
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

void ServerBrowser::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

std::vector<ServerRow> ServerBrowser::rows() const {
    std::lock_guard lock(mutex_);
    return rows_;
}

void ServerBrowser::run() {
    if (!netInit()) return;
    struct Query {
        std::unique_ptr<NetClient> net;
        Clock::time_point started, sent;
        bool done = true;
        Clock::time_point next;
    };
    std::vector<Query> queries;
    {
        std::lock_guard lock(mutex_);
        queries.resize(rows_.size());
    }
    while (running_) {
        auto now = Clock::now();
        for (size_t i = 0; i < queries.size(); ++i) {
            Query& q = queries[i];
            if (q.done) {
                if (now < q.next) continue;
                ServerEntry entry;
                {
                    std::lock_guard lock(mutex_);
                    entry = rows_[i].entry;
                }
                auto addr = parseAddress(entry.address);
                q.net = std::make_unique<NetClient>();
                q.done = false;
                q.started = now;
                q.sent = {};
                if (!addr || !q.net->connect(addr->host.c_str(), addr->port)) {
                    std::lock_guard lock(mutex_);
                    rows_[i].state = ServerRow::State::Offline;
                    q.done = true;
                    q.next = now + kRefreshEvery;
                }
                continue;
            }
            for (auto& ev : q.net->poll()) {
                if (ev.kind == NetEvent::Connected) {
                    auto b = encode(ServerInfoRequest{});
                    q.net->send(b, true);
                    q.sent = Clock::now();
                } else if (ev.kind == NetEvent::Packet) {
                    if (auto m = decode(ev.data); m && std::holds_alternative<ServerInfo>(*m)) {
                        int ping = static_cast<int>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - q.sent).count());
                        std::lock_guard lock(mutex_);
                        rows_[i].info = std::get<ServerInfo>(*m);
                        rows_[i].pingMs = ping;
                        rows_[i].state = ServerRow::State::Online;
                        q.done = true;
                    }
                }
            }
            if (!q.done && now - q.started > kQueryTimeout) {
                std::lock_guard lock(mutex_);
                rows_[i].state = ServerRow::State::Offline;
                rows_[i].pingMs = -1;
                q.done = true;
            }
            if (q.done) {
                q.net->disconnect();
                q.net.reset();
                q.next = now + kRefreshEvery;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (auto& q : queries)
        if (q.net) q.net->disconnect();
    queries.clear();
    netShutdown();
}

}  // namespace wok
