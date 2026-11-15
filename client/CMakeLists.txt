find_package(minhook CONFIG REQUIRED)

# Pure, game-independent client code (unit-tested): everything except the plugin entry and game glue.
file(GLOB WOK_CLIENT_CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(wok_client_core STATIC ${WOK_CLIENT_CORE_SOURCES})
target_include_directories(wok_client_core PUBLIC src)
target_link_libraries(wok_client_core PUBLIC wok_shared)

file(GLOB_RECURSE WOK_CLIENT_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER WOK_CLIENT_SOURCES EXCLUDE REGEX ".*/src/core/.*")
add_library(wok_client SHARED ${WOK_CLIENT_SOURCES})
target_link_libraries(wok_client PRIVATE wok_client_core minhook::minhook winhttp bcrypt)
