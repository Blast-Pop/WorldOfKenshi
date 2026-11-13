// Entry points called by OGRE: Kenshi loads every "Plugin=" listed in Plugins_x64.cfg and calls
// dllStartPlugin() during Ogre::Root initialisation, dllStopPlugin() at shutdown.
// Nothing here may throw into the game or touch OGRE objects beyond registering hooks.

#include "log.hpp"

#include <core/config.hpp>
#include <core/paths.hpp>

#include <wok/protocol.hpp>

#include <fstream>
#include <sstream>

namespace {

wok::ClientConfig loadOrCreateConfig() {
    const std::string path = wok::modFile(wok::modDir(), "wok_client.ini");
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        wok::ClientConfig defaults;
        std::ofstream(path, std::ios::binary) << wok::serializeConfig(defaults);
        wok::log("created default config %s", path.c_str());
        return defaults;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return wok::parseConfig(ss.str());
}

}  // namespace

extern "C" __declspec(dllexport) void dllStartPlugin() {
    try {
        wok::logInit();
        wok::log("World of Kenshi client loaded (protocol %u)", wok::WOK_PROTOCOL_VERSION);
        auto cfg = loadOrCreateConfig();
        wok::log("config: host=%s port=%u name=%s", cfg.host.c_str(), cfg.port, cfg.name.c_str());
    } catch (...) {
        wok::log("dllStartPlugin: unexpected exception");
    }
}

extern "C" __declspec(dllexport) void dllStopPlugin() {
    try {
        wok::log("World of Kenshi client unloading");
    } catch (...) {
    }
}
