#pragma once
// "Multiplayer" button on Kenshi's title screen + a server browser window: one row per server
// (version, name, description, players, ping), selection, Connect / Unlink / Close.

#include "../net_client.hpp"
#include "../server_browser.hpp"

#include <core/config.hpp>

#include <functional>
#include <string>

namespace wok {

class MultiplayerMenu {
public:
    // `saveConfig` persists the last used server; `connect` starts the connection to cfg.host/port.
    // `unlink` forgets this PC's server tokens (next connection asks Steam again).
    // `inGame` tells whether a world is loaded (the pause menu reuses the main menu layout).
    MultiplayerMenu(ClientConfig& cfg, ClientNet& net, std::function<void()> saveConfig, std::function<void()> connect,
                    std::function<void()> unlink, std::function<bool()> inGame, std::function<void()> disconnect);
    void update(float dt);  // game thread, every frame

private:
    void ensureButton();
    void openWindow();
    void closeWindow();
    void onClick(const std::string& widgetName);
    void refreshStatus();
    void refreshRows();
    void select(int row);
    void applyConnectedMenu();  // connected on the title screen: start the server game
    void applyPauseMenu();      // connected: no local save/load in the pause menu, Disconnect instead

    ClientConfig& cfg_;
    ClientNet& net_;
    std::function<void()> saveConfig_;
    std::function<void()> connect_;
    std::function<void()> unlink_;
    std::function<bool()> inGame_;
    std::function<void()> disconnect_;
    ServerBrowser browser_;
    int selected_ = -1;
    size_t rowCount_ = 0;
    float pollTimer_ = 0;
    float statusTimer_ = 0;
    bool mouseWasDown_ = false;
    void* pressedOn_ = nullptr;
    bool windowOpen_ = false;
    std::string lastStatus_;
    std::string error_;
    bool enteredGame_ = false;
    bool multiplayerWorld_ = false;   // the loaded world was started from multiplayer (until the title screen)
    int autoStep_ = 0;                // returning player: 1 = Begin clicked, 2 = editor confirmed
    int confirmWaits_ = 0;
    bool disconnectPending_ = false;  // Disconnect clicked, waiting for Kenshi's exit dialog answer
    // Loading screen covering the automated steps of a returning player until the world is ready.
    bool loadingShown_ = false;
    bool worldReady_ = false;
    float loadingTime_ = 0;
    std::string loadingText_;
    const char* lastTip_ = nullptr;
    void updateLoading(float dt);
    void showLoading(const std::string& text);
    void hideLoading();

public:
    // Called by the client once the returning player's character is back in place.
    void setWorldReady() { worldReady_ = true; }
};

}  // namespace wok
