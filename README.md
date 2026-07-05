# Dev-Day-26
Cyberpunk maze game built in C++ with Raylib — 3 levels, 3 enemy types (Bouncer, Chaser, Flashbanger), animated spike traps, procedural audio, and a live REST API leaderboard
# DEVDAY '26 – Maze Challenge

> Cyberpunk maze game built in C++ with Raylib — 3 levels, 3 enemy types, procedural audio, and a live leaderboard.

## About
DEVDAY '26 – Maze Challenge is a cyberpunk-themed maze game built from scratch in C++ using Raylib. Navigate 3 hand-crafted levels across a 40×30 tile grid, avoiding Bouncers that ricochet off walls, Chasers that steer toward you using real-time vector math, and Flashbangers that blind you and erase the maze walls for 3 seconds if you get too close. Animated spike traps pulse in and out using `sinf(GetTime())` — only deadly when extended. All sound effects are generated procedurally at runtime with no audio files. Scores are submitted asynchronously to a live REST API via `cpr` and `std::thread`, keeping the game loop non-blocking.

## Enemies
| Type | Behavior |
|------|----------|
| Bouncer | Travels in a straight line, bounces off walls |
| Chaser | Steers toward the player each frame using normalized vector math |
| Flashbanger | Blinds you and hides all walls for 3 seconds if you get within ~4.5 tiles |

## Features
- 3 escalating levels with hand-crafted tile maps
- Animated spike traps with pulse-based danger timing
- 100% procedurally generated sound effects (no audio files)
- Async REST API score submission via `cpr` + `std::thread`
- Fullscreen toggle (F11) at any time

## Controls
| Key | Action |
|-----|--------|
| Arrow Keys | Move |
| R | Restart (on game over) |
| ESC | Return to home screen |
| F11 | Toggle fullscreen |

## Built With
- C++ / Raylib
- CMake + MSVC (Windows)
- cpr (HTTP), nlohmann/json
