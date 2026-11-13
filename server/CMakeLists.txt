file(GLOB WOK_SERVER_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER WOK_SERVER_CORE_SOURCES EXCLUDE REGEX ".*/main\.cpp$")
add_library(wok_server_core STATIC ${WOK_SERVER_CORE_SOURCES})
target_include_directories(wok_server_core PUBLIC include)
target_link_libraries(wok_server_core PUBLIC wok_shared unofficial::sqlite3::sqlite3 httplib::httplib)
target_compile_definitions(wok_server_core PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)

add_executable(wok_server src/main.cpp)
target_link_libraries(wok_server PRIVATE wok_server_core)
