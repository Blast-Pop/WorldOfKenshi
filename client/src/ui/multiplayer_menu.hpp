#pragma once
// "Multiplayer" button on Kenshi's main menu + a window with the server address, the known
// servers (from wok_client.ini for now) and a Connect button.

#include "../net_client.hpp"

#include <core/config.hpp>

#include <functional>
#include <string>

namespace wok {

class MultiplayerMenu {
public:
    // `saveConfig` persists the last used address; `connect` starts the connection to cfg.host/port.
    // `unlink` forgets this PC's server tokens (next connection asks Steam again).
    // `inGame` tells whether a world is loaded (the pause menu reuses the main menu layout).
    MultiplayerMenu(ClientConfig& cfg, ClientNet& net, std::function<void()> saveConfig, std::function<void()> connect,
                    std::function<void()> unlink, std::function<bool()> inGame);
    void update(float dt);  // game thread, every frame

private:
    void ensureButton();
    void openWindow();
    void closeWindow();
    void onClick(const std::string& widgetName);
    void refreshStatus();
    void applyConnectedMenu();  // connected on the title screen: start the server game

    ClientConfig& cfg_;
    ClientNet& net_;
    std::function<void()> saveConfig_;
    std::function<void()> connect_;
    std::function<void()> unlink_;
    std::function<bool()> inGame_;
    float pollTimer_ = 0;
    float statusTimer_ = 0;
    bool mouseWasDown_ = false;
    void* pressedOn_ = nullptr;
    bool windowOpen_ = false;
    std::string lastStatus_;
    std::string error_;
    bool enteredGame_ = false;
};

}  // namespace wok
