# Builds the game's own C code (src/ at the repository root) for Android, with a
# small shim standing in for the Flipper Zero SDK.
cmake_minimum_required(VERSION 3.18)
project(echoids C)

# android/app/src/main/cpp -> repository root
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(GAME_SRC "${REPO_ROOT}/src")

add_library(echoids SHARED
    jni_bridge.c
    scanner_phone.c
    shim/shim_core.c
    shim/shim_gui.c
    ${GAME_SRC}/game.c
    ${GAME_SRC}/echoids.c
    ${GAME_SRC}/save.c
    ${GAME_SRC}/pac_store.c
    ${GAME_SRC}/scanner_parse.c
)

target_include_directories(echoids PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/shim"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${GAME_SRC}"
)
target_compile_definitions(echoids PRIVATE ECHOIDS_PHONE)
target_compile_options(echoids PRIVATE -std=gnu11 -Wall -Wno-unused-parameter)
target_link_libraries(echoids log)
