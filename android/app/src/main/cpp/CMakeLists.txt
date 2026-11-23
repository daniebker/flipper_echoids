# The Android app's native library: the game engine (engine/), built on the
# game's own rules from src/ at the repository root, with a small shim standing
# in for the Flipper SDK's storage, clock and random number calls.
cmake_minimum_required(VERSION 3.18)
project(echoids C)

# android/app/src/main/cpp -> repository root
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(GAME_SRC "${REPO_ROOT}/src")

add_library(echoids SHARED
    jni_bridge.c
    engine/engine.c
    engine/json.c
    shim/shim_core.c
    ${GAME_SRC}/game.c
    ${GAME_SRC}/session.c
    ${GAME_SRC}/save.c
    ${GAME_SRC}/pac_store.c
)

target_include_directories(echoids PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/shim"
    "${CMAKE_CURRENT_SOURCE_DIR}/engine"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${GAME_SRC}"
)
target_compile_options(echoids PRIVATE -std=gnu11 -Wall -Wno-unused-parameter)
target_link_libraries(echoids log)
