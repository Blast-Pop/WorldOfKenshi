cmake_minimum_required(VERSION 3.28)
project(WorldOfKenshi VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT on Windows: the client DLL must not depend on any redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

find_package(doctest CONFIG REQUIRED)
find_package(GameNetworkingSockets CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)

enable_testing()

add_subdirectory(shared)
add_subdirectory(server)
add_subdirectory(tools/testclient)
if(WIN32)
  add_subdirectory(client)
endif()

# One doctest executable aggregating every test file.
file(GLOB WOK_TEST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/tests/*.cpp
  ${CMAKE_SOURCE_DIR}/shared/tests/*.cpp
  ${CMAKE_SOURCE_DIR}/server/tests/*.cpp)
if(WIN32)
  file(GLOB WOK_CLIENT_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/client/tests/*.cpp)
  list(APPEND WOK_TEST_SOURCES ${WOK_CLIENT_TEST_SOURCES})
endif()
add_executable(wok_tests ${WOK_TEST_SOURCES})
target_link_libraries(wok_tests PRIVATE wok_shared wok_server_core doctest::doctest)
if(WIN32)
  target_link_libraries(wok_tests PRIVATE wok_client_core)
endif()
add_test(NAME wok_tests COMMAND wok_tests)

if(WIN32)
  # Prototype: Steam session ticket check by a game server (docs/m2-auth.md).
  add_executable(steamauth_probe tools/steamauth_probe/main.cpp)
endif()
