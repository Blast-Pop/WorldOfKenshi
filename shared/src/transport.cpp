#include <wok/transport.hpp>

#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace wok {

namespace {

// One lock for the whole library facade: the client runs its game connection and its server-browser
// queries on different threads, and the status callback routes through the shared maps below.
std::recursive_mutex& netMutex() {
    static std::recursive_mutex m;
    return m;
}

int g_initCount = 0;
// The library status callback is a plain function; route it to the owning object.
std::unordered_map<HSteamListenSocket, NetServer*> g_servers;
std::unordered_map<HSteamNetConnection, NetClient*> g_clients;

void onStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    if (info->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid) {
        auto it = g_servers.find(info->m_info.m_hListenSocket);
        if (it != g_servers.end()) it->second->onStatus(info);
        return;
    }
    auto it = g_clients.find(info->m_hConn);
    if (it != g_clients.end()) it->second->onStatus(info);
}

ISteamNetworkingSockets* sockets() { return SteamNetworkingSockets(); }

int sendFlags(bool reliable) {
    return reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoDelay;
}

std::vector<uint8_t> copyMessage(const SteamNetworkingMessage_t* m) {
    auto p = static_cast<const uint8_t*>(m->m_pData);
    return std::vector<uint8_t>(p, p + m->m_cbSize);
}

bool resolve(const char* host, uint16_t port, SteamNetworkingIPAddr& out) {
    out.Clear();
    std::string hp = std::string(host) + ":" + std::to_string(port);
    if (out.ParseString(hp.c_str())) return true;  // literal IPv4 / [IPv6]:port
    if (out.ParseString(host)) {                   // literal IPv6 without brackets
        out.m_port = port;
        return true;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0 || res == nullptr) return false;
    auto* sin = reinterpret_cast<sockaddr_in*>(res->ai_addr);
    out.SetIPv4(ntohl(sin->sin_addr.s_addr), port);
    freeaddrinfo(res);
    return true;
}

}  // namespace

bool netInit() {
    std::lock_guard netLock(netMutex());
    if (g_initCount++ > 0) return true;
    SteamNetworkingErrMsg err;
    if (!GameNetworkingSockets_Init(nullptr, err)) {
        std::fprintf(stderr, "GameNetworkingSockets_Init failed: %s\n", err);
        g_initCount = 0;
        return false;
    }
    SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(onStatusChanged);
    return true;
}

void netShutdown() {
    std::lock_guard netLock(netMutex());
    if (g_initCount == 0 || --g_initCount > 0) return;
    GameNetworkingSockets_Kill();
}

// ---------------------------------------------------------------- server

NetServer::~NetServer() {
    std::lock_guard netLock(netMutex());
    if (listen_ != k_HSteamListenSocket_Invalid) {
        g_servers.erase(listen_);
        sockets()->CloseListenSocket(listen_);
    }
    if (pollGroup_ != k_HSteamNetPollGroup_Invalid) sockets()->DestroyPollGroup(pollGroup_);
}

bool NetServer::listen(uint16_t port) {
    std::lock_guard netLock(netMutex());
    SteamNetworkingIPAddr addr;
    addr.Clear();
    addr.m_port = port;
    listen_ = sockets()->CreateListenSocketIP(addr, 0, nullptr);
    if (listen_ == k_HSteamListenSocket_Invalid) return false;
    pollGroup_ = sockets()->CreatePollGroup();
    g_servers[listen_] = this;
    return true;
}

void NetServer::onStatus(const void* p) {
    auto* info = static_cast<const SteamNetConnectionStatusChangedCallback_t*>(p);
    switch (info->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting:
            if (sockets()->AcceptConnection(info->m_hConn) != k_EResultOK) {
                sockets()->CloseConnection(info->m_hConn, 0, nullptr, false);
                return;
            }
            sockets()->SetConnectionPollGroup(info->m_hConn, pollGroup_);
            break;
        case k_ESteamNetworkingConnectionState_Connected:
            pending_.push_back({NetEvent::Connected, info->m_hConn, {}});
            break;
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            // Only report connections that had reached Connected.
            if (info->m_eOldState == k_ESteamNetworkingConnectionState_Connected)
                pending_.push_back({NetEvent::Disconnected, info->m_hConn, {}});
            sockets()->CloseConnection(info->m_hConn, 0, nullptr, false);
            break;
        default:
            break;
    }
}

void NetServer::send(ConnId conn, std::span<const uint8_t> data, bool reliable) {
    std::lock_guard netLock(netMutex());
    sockets()->SendMessageToConnection(conn, data.data(), static_cast<uint32>(data.size()), sendFlags(reliable),
                                       nullptr);
}

void NetServer::close(ConnId conn) {
    std::lock_guard netLock(netMutex());
    // Linger so a final reliable message (e.g. a rejection HelloAck) is still delivered.
    sockets()->CloseConnection(conn, 0, nullptr, true);
    pending_.push_back({NetEvent::Disconnected, conn, {}});
}

std::vector<NetEvent> NetServer::poll() {
    std::lock_guard netLock(netMutex());
    sockets()->RunCallbacks();
    SteamNetworkingMessage_t* msgs[64];
    for (;;) {
        int n = sockets()->ReceiveMessagesOnPollGroup(pollGroup_, msgs, 64);
        if (n <= 0) break;
        for (int i = 0; i < n; ++i) {
            pending_.push_back({NetEvent::Packet, msgs[i]->m_conn, copyMessage(msgs[i])});
            msgs[i]->Release();
        }
    }
    return std::exchange(pending_, {});
}

// ---------------------------------------------------------------- client

NetClient::~NetClient() { disconnect(); }

bool NetClient::connect(const char* host, uint16_t port) {
    std::lock_guard netLock(netMutex());
    disconnect();
    SteamNetworkingIPAddr addr;
    if (!resolve(host, port, addr)) return false;
    conn_ = sockets()->ConnectByIPAddress(addr, 0, nullptr);
    if (conn_ == k_HSteamNetConnection_Invalid) return false;
    g_clients[conn_] = this;
    return true;
}

void NetClient::onStatus(const void* p) {
    auto* info = static_cast<const SteamNetConnectionStatusChangedCallback_t*>(p);
    switch (info->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connected:
            connected_ = true;
            pending_.push_back({NetEvent::Connected, info->m_hConn, {}});
            break;
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            connected_ = false;
            pending_.push_back({NetEvent::Disconnected, info->m_hConn, {}});
            g_clients.erase(info->m_hConn);
            sockets()->CloseConnection(info->m_hConn, 0, nullptr, false);
            conn_ = k_HSteamNetConnection_Invalid;
            break;
        default:
            break;
    }
}

void NetClient::send(std::span<const uint8_t> data, bool reliable) {
    std::lock_guard netLock(netMutex());
    if (!connected_) return;
    sockets()->SendMessageToConnection(conn_, data.data(), static_cast<uint32>(data.size()), sendFlags(reliable),
                                       nullptr);
}

std::vector<NetEvent> NetClient::poll() {
    std::lock_guard netLock(netMutex());
    sockets()->RunCallbacks();
    if (conn_ != k_HSteamNetConnection_Invalid) {
        SteamNetworkingMessage_t* msgs[64];
        for (;;) {
            int n = sockets()->ReceiveMessagesOnConnection(conn_, msgs, 64);
            if (n <= 0) break;
            for (int i = 0; i < n; ++i) {
                pending_.push_back({NetEvent::Packet, conn_, copyMessage(msgs[i])});
                msgs[i]->Release();
            }
        }
    }
    return std::exchange(pending_, {});
}

void NetClient::disconnect() {
    std::lock_guard netLock(netMutex());
    if (conn_ == k_HSteamNetConnection_Invalid) return;
    g_clients.erase(conn_);
    sockets()->CloseConnection(conn_, 0, nullptr, true);
    conn_ = k_HSteamNetConnection_Invalid;
    connected_ = false;
}

}  // namespace wok
