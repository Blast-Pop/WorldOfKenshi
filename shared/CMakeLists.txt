file(GLOB WOK_SHARED_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(wok_shared STATIC ${WOK_SHARED_SOURCES})
target_include_directories(wok_shared PUBLIC include)
target_link_libraries(wok_shared PUBLIC GameNetworkingSockets::static)
