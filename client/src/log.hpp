#pragma once
// Thread-safe timestamped log to <Kenshi>\WorldOfKenshi\wok_client.log.

#include <string>

namespace wok {

// Directory "<folder of kenshi_x64.exe>\WorldOfKenshi", created on first call.
const std::string& modDir();
void logInit();
void log(const char* fmt, ...);

}  // namespace wok
