cmake_minimum_required(VERSION 3.16)
project(maze_game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies via vcpkg ────────────────────────────────────────────────────
find_package(raylib CONFIG REQUIRED)
find_package(cpr    CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(maze_game src/maze.cpp)

target_link_libraries(maze_game PRIVATE
    raylib
    cpr::cpr
    nlohmann_json::nlohmann_json
)

# ── Windows: link winmm + opengl32 (raylib needs them) ───────────────────────
if(WIN32)
    target_link_libraries(maze_game PRIVATE winmm opengl32 gdi32)
endif()

# ── Copy player.png next to the exe after build ───────────────────────────────
add_custom_command(TARGET maze_game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/player.png"
        "$<TARGET_FILE_DIR:maze_game>/player.png"
    COMMENT "Copying player.png to output directory"
)
