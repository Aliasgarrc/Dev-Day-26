// maze.cpp  –  DEVDAY '26  Maze Challenge
// Integrated: name fetch, score submit, guest mode

// ── Must come BEFORE any Windows headers (including those pulled by cpr) ─────
// Exclude rarely-used Windows APIs that conflict with raylib
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#define NOUSER
// Undefine Windows macros that clash with raylib function names
#ifdef DrawText
#undef DrawText
#endif
#ifdef PlaySound
#undef PlaySound
#endif
#ifdef CloseWindow
#undef CloseWindow
#endif
#ifdef ShowCursor
#undef ShowCursor
#endif

// ── CPR / JSON first; Windows API surface is trimmed by defines above ─────────
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <string>
#include <mutex>

// ── Undefine again after cpr (cpr pulls windows.h internally) ────────────────
#ifdef DrawText
#undef DrawText
#endif
#ifdef PlaySound
#undef PlaySound
#endif
#ifdef CloseWindow
#undef CloseWindow
#endif
#ifdef ShowCursor
#undef ShowCursor
#endif

#include <raylib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ── MSVC fix: compound Color literal helper ───────────────────────────────────
// MSVC doesn't support C99 compound literals like MKCOLOR(r,g,b,a) in C++ mode.
// Use this inline helper instead — zero overhead, same result.
static inline Color CLITERAL_COLOR(unsigned char r, unsigned char g,
                                    unsigned char b, unsigned char a)
{ Color c; c.r=r; c.g=g; c.b=b; c.a=a; return c; }
#define MKCOLOR(r,g,b,a) CLITERAL_COLOR(r,g,b,a)

// ─── Defines ──────────────────────────────────────────────────────────────────
#define num_levels             3
#define screen_width           800
#define screen_height          600
#define tile_size              20
#define map_width              (screen_width  / tile_size)   // 40
#define map_height             (screen_height / tile_size)   // 30
#define MAX_ENEMIES            6
#define INVINCIBILITY_DURATION 120
#define FLASHBANG_DURATION     180
#define MAX_SPIKES             8

#define ENEMY_BOUNCER     0
#define ENEMY_CHASER      1
#define ENEMY_FLASHBANGER 2

#ifndef PI
#define PI 3.14159265358979f
#endif

// ─── API config ───────────────────────────────────────────────────────────────
static const std::string BASE_URL  = "https://minigame-manager-cc533de7be66.herokuapp.com";
static const std::string API_KEY   = "mgk_0ca82f437ed794374321f3fee162123f466d1cdf8ca3264deb3ca230cceade49";   // <-- replace
static const std::string GAME_ID   = "1479d3bb-6d86-4f01-9da5-6f6663b02513";            // <-- replace

// ─── DEVDAY '26 Color Palette ─────────────────────────────────────────────────
#define DD_BG         MKCOLOR( 15,   3,   3, 255)
#define DD_FLOOR      MKCOLOR( 28,   6,   6, 255)
#define DD_WALL       MKCOLOR( 90,  10,  10, 255)
#define DD_WALL_H     MKCOLOR(140,  20,  20, 255)
#define DD_BORDER     MKCOLOR(180,  20,  20, 255)
#define DD_BORDER_H   MKCOLOR(220,  50,  50, 255)
#define DD_RED        MKCOLOR(200,  20,  20, 255)
#define DD_RED_BRIGHT MKCOLOR(255,  60,  60, 255)
#define DD_SILVER     MKCOLOR(210, 210, 215, 255)
#define DD_SILVER_DIM MKCOLOR(140, 135, 140, 255)
#define DD_HUD_BG     MKCOLOR(  8,   0,   0, 220)
#define DD_GREEN      MKCOLOR( 20, 200,  60, 255)
#define DD_YELLOW     MKCOLOR(220, 180,  10, 255)

// ─── Session state ────────────────────────────────────────────────────────────
static bool        guest_mode      = false;
static std::string player_code     = "";       // e.g. "U001"
static std::string player_name     = "";       // fetched from API
static double      session_start   = 0.0;      // GetTime() at game start

// async name fetch
static std::atomic<bool>  fetch_done{false};
static std::atomic<bool>  fetch_ok{false};
static std::mutex          fetch_mutex;
static std::string         fetch_result_name;

// async score submit
static std::atomic<bool>  submit_done{false};
static std::atomic<bool>  submit_ok{false};
static std::mutex          submit_mutex;
static std::string         submit_result_msg;

// ─── Global game state ────────────────────────────────────────────────────────
float time_left            = 80.0f;
float max_time             = 80.0f;
int   loss_display_frames  = 0;
bool  game_over            = false;
bool  player_won           = false;
bool  ran_out_of_time      = false;
int   current_level        = 0;
int   lives                = 3;
int   invincibility_frames = 0;
int   flashbang_frames     = 0;
float flashbang_white      = 0.0f;
int   total_score          = 0;   // accumulated across levels

const int start_tile_x = 1;
const int start_tile_y = 1;

RenderTexture2D gameTarget;

Sound snd_hit;
Sound snd_bang;
Sound snd_levelup;
Sound snd_gameover;

// ─── Structs ──────────────────────────────────────────────────────────────────
typedef struct {
    Vector2   position;
    float     speed;
    Texture2D sprite;
} PLAYER;

typedef struct {
    Vector2 position;
    float   speed;
    float   dirX;
    float   dirY;
    bool    active;
    int     type;
} ENEMY;

typedef struct {
    int   tx, ty;
    bool  active;
    float anim;
} SPIKE;

ENEMY enemies[MAX_ENEMIES];
SPIKE spikes[MAX_SPIKES];

// ─── Level data (unchanged) ───────────────────────────────────────────────────
const int levels[num_levels][map_height][map_width] = {
    {
        {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2},
        {2,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0,0,0,0,2},
        {2,0,1,1,1,0,1,0,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,0,1,1,0,1,0,1,0,1,1,1,1,1,0,2},
        {2,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0,0,2},
        {2,1,1,0,1,1,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,0,1,1,1,0,2},
        {2,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,1,0,0,0,2},
        {2,0,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,1,1,1,0,1,0,1,0,1,1,2},
        {2,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,1,0,1,0,1,0,0,0,0,0,0,0,0,1,0,0,0,2},
        {2,0,1,0,1,0,1,0,1,0,1,1,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,1,1,1,1,1,1,1,1,1,0,2},
        {2,0,1,0,1,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,2},
        {2,0,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,1,0,1,0,2},
        {2,0,0,0,0,0,0,0,1,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,1,0,1,0,0,0,1,0,2},
        {2,0,1,1,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,0,1,0,1,1,1,0,2},
        {2,0,1,0,0,0,0,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0,1,0,1,0,1,0,1,0,0,0,0,1,0,1,0,0,0,2},
        {2,0,1,0,1,1,1,1,1,0,1,0,1,0,1,1,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,1,1,1,0,1,1,1,1,2},
        {2,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,1,0,0,0,2},
        {2,0,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,0,1,1,0,1,0,1,0,2},
        {2,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,1,0,0,1,0,1,0,2},
        {2,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,1,0,1,0,2},
        {2,0,0,0,1,0,1,0,1,0,0,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0,0,1,0,0,0,1,0,0,1,0,2},
        {2,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,1,1,1,1,0,1,1,0,2},
        {2,0,1,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0,1,0,0,0,0,0,0,0,1,0,0,2},
        {2,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,1,1,0,1,1,1,1,1,0,1,0,1,0,1,1,1,1,1,1,1,0,1,2},
        {2,0,1,0,1,0,1,0,1,0,1,0,1,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1,2},
        {2,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,0,1,1,1,0,1,2},
        {2,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0,0,0,1,2},
        {2,0,1,0,1,0,0,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,1,1,2},
        {2,0,1,0,1,1,1,1,1,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0,1,0,1,0,1,0,1,0,0,0,1,0,0,0,0,2},
        {2,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,1,0,1,1,1,0,1,0,0,0,1,0,0,0,1,0,1,1,1,1,1,1,3,2},
        {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2}
    },
    {
        {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2},
        {2,0,0,1,0,0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,1,0,1,0,0,1,0,0,0,1,0,0,1,0,0,2},
        {2,1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,1,0,1,0,1,0,1,1,0,1,0,1,0,1,1,0,1,0,1,2},
        {2,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,1,2},
        {2,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,1,1,1,2},
        {2,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,2},
        {2,1,1,1,1,0,1,0,1,1,1,1,1,1,1,1,0,1,0,1,0,1,1,1,0,1,1,1,1,1,1,1,0,1,1,1,1,1,0,2},
        {2,0,0,0,1,0,0,0,1,0,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,2},
        {2,0,1,0,1,1,1,1,1,0,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,2},
        {2,0,1,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,2},
        {2,0,1,1,1,1,1,1,1,0,1,0,0,1,1,1,1,1,1,1,1,1,0,1,0,1,1,1,0,1,1,1,1,1,0,1,0,1,1,2},
        {2,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,2},
        {2,1,1,0,1,1,1,0,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,0,2},
        {2,0,0,0,1,0,0,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,2},
        {2,0,1,1,1,0,1,1,1,0,1,0,1,0,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,0,2},
        {2,0,0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,2},
        {2,1,1,1,1,0,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,0,2},
        {2,0,0,0,1,0,1,0,0,0,0,0,0,0,1,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,1,0,2},
        {2,0,1,0,1,0,1,1,1,1,1,1,1,1,1,0,1,0,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,0,1,0,2},
        {2,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,2},
        {2,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,0,1,1,1,1,1,0,2},
        {2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,0,0,0,0,2},
        {2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,0,1,0,1,0,1,1,1,1,1,1,1,0,2},
        {2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1,0,2},
        {2,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0,2},
        {2,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,2},
        {2,0,1,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,2},
        {2,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,2},
        {2,0,1,1,1,0,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,0,3,2},
        {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2}
    },
    {
        {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2},
        {2,0,0,1,0,1,0,1,0,1,0,1,0,0,1,0,1,0,1,0,1,0,1,0,1,0,0,1,0,1,0,1,0,1,0,0,1,0,0,2},
        {2,1,0,1,0,1,0,1,0,1,0,1,0,1,1,0,1,0,1,0,1,0,1,0,1,0,1,1,0,1,0,1,0,1,0,1,1,0,1,2},
        {2,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,2},
        {2,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,2},
        {2,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,2},
        {2,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,0,1,1,0,2},
        {2,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,2},
        {2,0,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,1,1,1,0,2},
        {2,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,2},
        {2,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,0,1,0,2},
        {2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,2},
        {2,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,1,1,1,0,1,1,1,1,2},
        {2,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,2},
        {2,0,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,0,1,1,1,1,1,0,2},
        {2,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,2},
        {2,0,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,1,1,1,0,1,1,1,1,1,0,1,0,2},
        {2,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,2},
        {2,1,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,0,1,1,1,1,1,0,1,1,1,0,2},
        {2,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,2},
        {2,0,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,1,1,1,0,1,1,1,1,1,0,1,1,1,0,1,0,2},
        {2,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,2},
        {2,0,1,1,1,0,1,1,1,1,0,1,1,1,1,1,1,1,1,0,1,0,1,1,0,1,1,1,1,1,0,1,1,1,0,1,1,1,1,2},
        {2,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,1,2},
        {2,0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,0,1,1,1,1,1,0,1,1,1,0,1,1,1,1,0,1,2},
        {2,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,2},
        {2,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,0,1,1,1,1,1,0,1,1,1,0,1,1,1,1,0,1,1,0,2},
        {2,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,1,0,2},
        {2,0,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,0,1,0,1,1,1,0,1,0,1,0,1,0,1,1,0,1,0,0,0,3,2},
        {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2}
    }
};

// ─── Prototypes ───────────────────────────────────────────────────────────────
void PresentFrame(void);
void DrawMaze(void);
void DrawPlayer(PLAYER *player);
void UpdatePlayer(PLAYER *player);
void resetplayer(PLAYER *player);
void Checkwincondition(PLAYER *player);
void UpdateTimeandcheckgameover(void);
void InitEnemies(void);
void UpdateEnemies(PLAYER *player);
void DrawEnemies(void);
void InitSpikes(void);
void UpdateSpikes(PLAYER *player);
void DrawSpikes(void);
void DrawHUD(void);
void DrawSplashScreen(void);
void DrawDD26Logo(int cx, int cy, float scale, float alpha);
void DrawWatermark(float alpha);
bool IsTileBlocked(int tx, int ty);
void InitSounds(void);
Sound MakeNoise(float dur, float vol);
Sound MakeSweep(float f0, float f1, float dur, float vol);
// Guide helpers
static bool AnyGameKey(void);
static void DrawObstaclesGuide(float alpha);
static void DrawGuideIcon_Bouncer(int cx, int cy, int sz);
static void DrawGuideIcon_Chaser(int cx, int cy, int sz);
static void DrawGuideIcon_Flashbanger(int cx, int cy, int sz);
static void DrawGuideIcon_Spike(int cx, int cy, int r);

// ─── NEW: Login / entry screen ────────────────────────────────────────────────
// Returns true when user has chosen guest or entered a valid code.
// Draws its own loop inside.
void AsyncFetchName(const std::string &code);
void AsyncSubmitScore(const std::string &code, int score, float play_time);
int  CalcScore(void);
void DrawLoginScreen(void);

// ─── PresentFrame ─────────────────────────────────────────────────────────────
void PresentFrame(void) {
    EndTextureMode();
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    float scale = fminf((float)sw / screen_width, (float)sh / screen_height);
    float ox = (sw - screen_width  * scale) * 0.5f;
    float oy = (sh - screen_height * scale) * 0.5f;
    BeginDrawing();
    ClearBackground(BLACK);
    Rectangle src_rect; src_rect.x=0; src_rect.y=0; src_rect.width=(float)screen_width; src_rect.height=-(float)screen_height;
    Rectangle dst_rect; dst_rect.x=ox; dst_rect.y=oy; dst_rect.width=screen_width*scale; dst_rect.height=screen_height*scale;
    Vector2 origin; origin.x=0; origin.y=0;
    DrawTexturePro(gameTarget.texture, src_rect, dst_rect, origin, 0.0f, WHITE);
    EndDrawing();
}

// ─── Sound generation ─────────────────────────────────────────────────────────
Sound MakeNoise(float dur, float vol) {
    int sr = 44100, n = (int)(sr * dur);
    short *data = (short *)malloc(n * sizeof(short));
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr, env = fmaxf(0.0f, 1.0f - t / dur);
        float v = ((float)(rand() % 65536) / 32768.0f) - 1.0f;
        data[i] = (short)(v * env * 32767.0f * vol);
    }
    Wave w; w.frameCount=(unsigned int)n; w.sampleRate=44100; w.sampleSize=16; w.channels=1; w.data=data;
    Sound s = LoadSoundFromWave(w); free(data); return s;
}

Sound MakeSweep(float f0, float f1, float dur, float vol) {
    int sr = 44100, n = (int)(sr * dur);
    short *data = (short *)malloc(n * sizeof(short));
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr, env = fmaxf(0.0f, 1.0f - t / dur);
        float freq = f0 + (f1 - f0) * t / dur;
        phase += 2.0f * PI * freq / (float)sr;
        data[i] = (short)(sinf(phase) * env * 32767.0f * vol);
    }
    Wave w; w.frameCount=(unsigned int)n; w.sampleRate=44100; w.sampleSize=16; w.channels=1; w.data=data;
    Sound s = LoadSoundFromWave(w); free(data); return s;
}

void InitSounds(void) {
    snd_hit      = MakeNoise(0.15f, 0.75f);
    snd_bang     = MakeSweep(1400.0f, 120.0f, 0.45f, 0.90f);
    snd_levelup  = MakeSweep(250.0f,  900.0f, 0.55f, 0.80f);
    snd_gameover = MakeSweep(380.0f,   60.0f, 1.20f, 0.80f);
}

// ─── DrawDD26Logo / DrawWatermark (unchanged) ─────────────────────────────────
void DrawDD26Logo(int cx, int cy, float scale, float alpha) {
    Color c1 = Fade(MKCOLOR(200,15,15,255),alpha), c2 = Fade(MKCOLOR(240,50,50,255),alpha);
    Color c3 = Fade(MKCOLOR(140,8,8,255),alpha),   c4 = Fade(MKCOLOR(255,80,80,255),alpha);
    int s = (int)(scale * 40);
    DrawTriangle([&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy-s*2); return _v;}(),
                 [&](){Vector2 _v; _v.x=(float)(cx+s); _v.y=(float)cy; return _v;}(),
                 [&](){Vector2 _v; _v.x=(float)(cx-s); _v.y=(float)cy; return _v;}(), c1);
    DrawTriangle([&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy-s*2); return _v;}(),
                 [&](){Vector2 _v; _v.x=(float)(cx+s*2); _v.y=(float)(cy+s); return _v;}(),
                 [&](){Vector2 _v; _v.x=(float)(cx+s); _v.y=(float)cy; return _v;}(), c2);
    DrawTriangle([&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy-s*2); return _v;}(),
                 [&](){Vector2 _v; _v.x=(float)(cx-s); _v.y=(float)cy; return _v;}(),
                 [&](){Vector2 _v; _v.x=(float)(cx-s*2); _v.y=(float)(cy+s); return _v;}(), c3);
    DrawTriangle([&](){Vector2 _v; _v.x=(float)(cx-s); _v.y=(float)cy; return _v;}(),
                 [&](){Vector2 _v; _v.x=(float)(cx+s); _v.y=(float)cy; return _v;}(),
                 [&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy+s*2); return _v;}(), c4);
    DrawTriangle([&](){Vector2 _v; _v.x=(float)(cx-s*2); _v.y=(float)(cy+s); return _v;}(),
                 [&](){Vector2 _v; _v.x=(float)(cx-s); _v.y=(float)cy; return _v;}(),
                 [&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy+s*2); return _v;}(), c3);
    DrawTriangle([&](){Vector2 _v; _v.x=(float)(cx+s); _v.y=(float)cy; return _v;}(),
                 [&](){Vector2 _v; _v.x=(float)(cx+s*2); _v.y=(float)(cy+s); return _v;}(),
                 [&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy+s*2); return _v;}(), c2);
}

void DrawWatermark(float alpha) {
    Color wc = Fade(MKCOLOR(100,8,8,255), alpha);
    int stepX = 90, stepY = 36;
    for (int y = -stepY; y < screen_height + stepY; y += stepY)
        for (int x = -stepX; x < screen_width + stepX; x += stepX) {
            int ox = (y / stepY % 2 == 0) ? 0 : stepX / 2;
            DrawText("DD26", x + ox, y, 22, wc);
        }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ─── ASYNC API HELPERS ────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════

void AsyncFetchName(const std::string &code) {
    fetch_done = false; fetch_ok = false;
    std::thread([code]() {
        cpr::Response r = cpr::Get(
            cpr::Url{ BASE_URL + "/api/participants/" + code },
            cpr::Timeout{ 8000 }
        );
        std::lock_guard<std::mutex> lk(fetch_mutex);
        if (r.status_code == 200) {
            try {
                auto data = nlohmann::json::parse(r.text);
                fetch_result_name = data.value("fullName", "");
                fetch_ok = !fetch_result_name.empty();
            } catch (...) { fetch_ok = false; }
        } else { fetch_ok = false; }
        fetch_done = true;
    }).detach();
}

int CalcScore(void) {
    // Base: time remaining bonus + lives bonus
    int score = (int)(time_left * 10) + lives * 200;
    return score > 0 ? score : 0;
}

void AsyncSubmitScore(const std::string &code, int score, float play_time) {
    submit_done = false; submit_ok = false;
    std::thread([code, score, play_time]() {
        nlohmann::json payload = {
            {"userCode",  code},
            {"gameId",    GAME_ID},
            {"score",     score},
            {"playTime",  play_time},
            {"metadata",  { {"levels_cleared", current_level} }}
        };
        cpr::Response r = cpr::Post(
            cpr::Url{ BASE_URL + "/api/scores" },
            cpr::Header{
                {"Content-Type", "application/json"},
                {"x-api-key",    API_KEY}
            },
            cpr::Body{ payload.dump() },
            cpr::Timeout{ 8000 }
        );
        std::lock_guard<std::mutex> lk(submit_mutex);
        if (r.status_code == 200) {
            submit_ok = true;
            submit_result_msg = "Score submitted!";
        } else {
            submit_ok = false;
            submit_result_msg = "Submit failed (" + std::to_string(r.status_code) + ")";
        }
        submit_done = true;
    }).detach();
}

// ═══════════════════════════════════════════════════════════════════════════════
// ─── LOGIN / ENTRY SCREEN ─────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════
// States: 0=choosing mode, 1=typing code, 2=fetching, 3=fetch error
void DrawLoginScreen(void) {
    int    state       = 0;          // 0=menu, 1=input, 2=fetching, 3=error
    char   inputBuf[16] = {};
    int    inputLen    = 0;
    int    cursor_blink= 0;
    std::string errMsg;
    int    errTimer    = 0;

    while (!WindowShouldClose()) {
        cursor_blink++;

        // ── state 0: mode selection ──────────────────────────────────────────
        if (state == 0) {
            if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1)) {
                state = 1; inputLen = 0; memset(inputBuf, 0, sizeof(inputBuf));
            }
            if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2)) {
                guest_mode  = true;
                player_name = "Guest";
                player_code = "";
                return;   // done — start game
            }
        }

        // ── state 1: typing code ─────────────────────────────────────────────
        else if (state == 1) {
            // Backspace
            if (IsKeyPressed(KEY_BACKSPACE) && inputLen > 0)
                inputBuf[--inputLen] = 0;

            // Escape → back to menu
            if (IsKeyPressed(KEY_ESCAPE)) { state = 0; inputLen = 0; memset(inputBuf,0,sizeof(inputBuf)); }

            // Character input (letters + digits, max 10 chars)
            int ch = GetCharPressed();
            while (ch > 0) {
                if (inputLen < 10 && ((ch >= 'A' && ch <= 'Z') ||
                                      (ch >= 'a' && ch <= 'z') ||
                                      (ch >= '0' && ch <= '9'))) {
                    inputBuf[inputLen++] = (char)toupper(ch);
                }
                ch = GetCharPressed();
            }

            // Enter → submit
            if (IsKeyPressed(KEY_ENTER) && inputLen >= 2) {
                player_code = std::string(inputBuf, inputLen);
                AsyncFetchName(player_code);
                state = 2;
            }
        }

        // ── state 2: waiting for API ─────────────────────────────────────────
        else if (state == 2) {
            if (fetch_done) {
                if (fetch_ok) {
                    std::lock_guard<std::mutex> lk(fetch_mutex);
                    player_name = fetch_result_name;
                    guest_mode  = false;
                    return;   // success — start game
                } else {
                    errMsg  = "Code not found. Try again or press ESC.";
                    errTimer= 0;
                    state   = 3;
                }
            }
        }

        // ── state 3: error shown ─────────────────────────────────────────────
        else if (state == 3) {
            errTimer++;
            if (IsKeyPressed(KEY_ESCAPE)) {
                state = 0; inputLen = 0; memset(inputBuf, 0, sizeof(inputBuf));
            }
            if (IsKeyPressed(KEY_ENTER) || errTimer > 180) {
                state = 1; inputLen = 0; memset(inputBuf, 0, sizeof(inputBuf));
            }
        }

        // ── Draw ─────────────────────────────────────────────────────────────
        BeginTextureMode(gameTarget);
        ClearBackground(DD_BG);
        DrawWatermark(0.18f);

        // Top / bottom bars
        DrawRectangle(0, 0, screen_width, 60, Fade(BLACK, 0.5f));
        DrawRectangle(0, screen_height - 60, screen_width, 60, Fade(BLACK, 0.5f));

        DrawDD26Logo(680, 480, 1.8f, 0.9f);

        // Title
        const char *title = "DEVDAY '26";
        int tw = MeasureText(title, 68);
        DrawText(title, screen_width/2 - tw/2 + 3, 80 + 3, 68, Fade(BLACK, 0.7f));
        DrawText(title, screen_width/2 - tw/2,     80,     68, Fade(DD_SILVER, 1.0f));
        const char *sub = "MAZE  CHALLENGE";
        int sw2 = MeasureText(sub, 22);
        DrawText(sub, screen_width/2 - sw2/2, 158, 22, DD_RED_BRIGHT);
        DrawRectangle(screen_width/2 - 160, 188, 320, 2, DD_RED);

        if (state == 0) {
            // Mode selection box
            DrawRectangle(screen_width/2 - 200, 220, 400, 160,
                          Fade(MKCOLOR(20,0,0,255), 0.85f));
            DrawRectangleLines(screen_width/2 - 200, 220, 400, 160, DD_RED);

            const char *m1 = "[1]  Enter Player Code";
            const char *m2 = "[2]  Play as Guest";
            DrawText(m1, screen_width/2 - MeasureText(m1,18)/2, 252, 18, DD_SILVER);
            DrawText(m2, screen_width/2 - MeasureText(m2,18)/2, 290, 18, DD_SILVER_DIM);

            DrawText("Guest scores are not saved.",
                     screen_width/2 - MeasureText("Guest scores are not saved.",13)/2,
                     340, 13, MKCOLOR(80,60,60,255));
        }
        else if (state == 1) {
            // Code input box
            DrawRectangle(screen_width/2 - 200, 220, 400, 160,
                          Fade(MKCOLOR(20,0,0,255), 0.85f));
            DrawRectangleLines(screen_width/2 - 200, 220, 400, 160, DD_RED);

            DrawText("Enter your player code:", screen_width/2 - 110, 235, 16, DD_SILVER_DIM);

            // Input field
            DrawRectangle(screen_width/2 - 140, 260, 280, 36,
                          Fade(MKCOLOR(5,0,0,255), 0.9f));
            DrawRectangleLines(screen_width/2 - 140, 260, 280, 36, DD_RED);

            char display[18]; 
            snprintf(display, sizeof(display), "%s%s", inputBuf,
                     (cursor_blink / 20) % 2 == 0 ? "_" : " ");
            DrawText(display, screen_width/2 - MeasureText(display,20)/2, 268, 20, DD_SILVER);

            DrawText("ENTER = confirm    ESC = back",
                     screen_width/2 - MeasureText("ENTER = confirm    ESC = back",13)/2,
                     312, 13, MKCOLOR(80,60,60,255));
            DrawText("(letters & numbers, max 10 chars)",
                     screen_width/2 - MeasureText("(letters & numbers, max 10 chars)",12)/2,
                     332, 12, MKCOLOR(60,45,45,255));
        }
        else if (state == 2) {
            DrawRectangle(screen_width/2 - 200, 220, 400, 100,
                          Fade(MKCOLOR(20,0,0,255), 0.85f));
            DrawRectangleLines(screen_width/2 - 200, 220, 400, 100, DD_RED);

            float dots = fmodf((float)GetTime() * 2.0f, 4.0f);
            char fetching[32];
            snprintf(fetching, sizeof(fetching), "Looking up code%s",
                     dots < 1 ? "." : dots < 2 ? ".." : dots < 3 ? "..." : "....");
            DrawText(fetching, screen_width/2 - MeasureText(fetching,18)/2, 258, 18, DD_YELLOW);
            DrawText(player_code.c_str(),
                     screen_width/2 - MeasureText(player_code.c_str(),14)/2,
                     286, 14, DD_SILVER_DIM);
        }
        else if (state == 3) {
            DrawRectangle(screen_width/2 - 220, 220, 440, 100,
                          Fade(MKCOLOR(20,0,0,255), 0.85f));
            DrawRectangleLines(screen_width/2 - 220, 220, 440, 100, DD_RED);

            DrawText(errMsg.c_str(),
                     screen_width/2 - MeasureText(errMsg.c_str(),16)/2,
                     255, 16, DD_RED_BRIGHT);
            DrawText("ENTER = retry    ESC = main menu",
                     screen_width/2 - MeasureText("ENTER = retry    ESC = main menu",13)/2,
                     290, 13, DD_SILVER_DIM);
        }

        PresentFrame();
    }
}

// ─── Guide icon helpers ───────────────────────────────────────────────────────
static bool AnyGameKey(void) {
    return IsKeyPressed(KEY_SPACE)||IsKeyPressed(KEY_ENTER)||
           IsKeyPressed(KEY_UP)   ||IsKeyPressed(KEY_DOWN) ||
           IsKeyPressed(KEY_LEFT) ||IsKeyPressed(KEY_RIGHT);
}

static void DrawGuideIcon_Bouncer(int cx, int cy, int sz) {
    DrawRectangle(cx-sz/2,     cy-sz/2,     sz,     sz,     MKCOLOR(60,5,5,255));
    DrawRectangle(cx-sz/2+2,   cy-sz/2+2,   sz-4,   sz-4,   MKCOLOR(200,15,15,255));
    DrawRectangle(cx-sz/2+2,   cy-sz/2+2,   sz-4,   2,      MKCOLOR(255,60,60,255));
    int ew=sz/5, eh=sz/4;
    DrawRectangle(cx-sz/4-ew/2, cy-sz/8,       ew, eh, WHITE);
    DrawRectangle(cx+sz/4-ew/2, cy-sz/8,       ew, eh, WHITE);
    DrawRectangle(cx-sz/4-ew/2+1, cy-sz/8+1, ew-2, eh-2, MKCOLOR(180,0,0,255));
    DrawRectangle(cx+sz/4-ew/2+1, cy-sz/8+1, ew-2, eh-2, MKCOLOR(180,0,0,255));
}

static void DrawGuideIcon_Chaser(int cx, int cy, int sz) {
    DrawRectangle(cx-sz/2,   cy-sz/2,   sz,   sz,   MKCOLOR(5,15,60,220));
    DrawRectangle(cx-sz/2+2, cy-sz/2+2, sz-4, sz-4, MKCOLOR(30,80,220,180));
    DrawRectangle(cx-sz/2+2, cy-sz/2+2, sz-4, 2,    MKCOLOR(100,160,255,255));
    int ew=sz/5, eh=sz/4;
    DrawRectangle(cx-sz/4-ew/2, cy-sz/8,       ew, eh, WHITE);
    DrawRectangle(cx+sz/4-ew/2, cy-sz/8,       ew, eh, WHITE);
    DrawRectangle(cx-sz/4-ew/2+1, cy-sz/8+1, ew-2, eh-2, MKCOLOR(0,0,180,255));
    DrawRectangle(cx+sz/4-ew/2+1, cy-sz/8+1, ew-2, eh-2, MKCOLOR(0,0,180,255));
    for (int d=0;d<3;d++)
        DrawRectangle(cx-sz/2+3+d*(sz/3), cy+sz/2-1, 3, 4, MKCOLOR(30,80,220,130));
}

static void DrawGuideIcon_Flashbanger(int cx, int cy, int sz) {
    DrawRectangle(cx-sz/2, cy-sz/2, sz, sz, MKCOLOR(80,40,0,255));
    DrawRectangle(cx-2, cy-sz/2+2, 4, sz-4, MKCOLOR(255,200,0,255));
    DrawRectangle(cx-sz/2+2, cy-2, sz-4, 4, MKCOLOR(255,200,0,255));
    DrawRectangle(cx-3, cy-3, 6, 6, WHITE);
    DrawCircleLines((float)cx,(float)cy,(float)(sz*9/10),Fade(MKCOLOR(255,200,0,255),0.35f));
}

static void DrawGuideIcon_Spike(int cx, int cy, int r) {
    DrawRectangle(cx-r,cy-r,r*2,r*2,Fade(MKCOLOR(255,210,10,255),0.12f));
    Color col=MKCOLOR(255,210,10,255);
    // 4 cardinal spikes
    DrawTriangle([&](){Vector2 v; v.x=(float)cx;    v.y=(float)(cy-r); return v;}(),
                 [&](){Vector2 v; v.x=(float)(cx-2); v.y=(float)cy;    return v;}(),
                 [&](){Vector2 v; v.x=(float)(cx+2); v.y=(float)cy;    return v;}(), col);
    DrawTriangle([&](){Vector2 v; v.x=(float)cx;    v.y=(float)(cy+r); return v;}(),
                 [&](){Vector2 v; v.x=(float)(cx+2); v.y=(float)cy;    return v;}(),
                 [&](){Vector2 v; v.x=(float)(cx-2); v.y=(float)cy;    return v;}(), col);
    DrawTriangle([&](){Vector2 v; v.x=(float)(cx-r); v.y=(float)cy;    return v;}(),
                 [&](){Vector2 v; v.x=(float)cx;     v.y=(float)(cy-2);return v;}(),
                 [&](){Vector2 v; v.x=(float)cx;     v.y=(float)(cy+2);return v;}(), col);
    DrawTriangle([&](){Vector2 v; v.x=(float)(cx+r); v.y=(float)cy;    return v;}(),
                 [&](){Vector2 v; v.x=(float)cx;     v.y=(float)(cy+2);return v;}(),
                 [&](){Vector2 v; v.x=(float)cx;     v.y=(float)(cy-2);return v;}(), col);
    // diagonal mini-spikes
    int rd=r*6/10;
    DrawTriangle([&](){Vector2 v; v.x=(float)(cx-rd); v.y=(float)(cy-rd); return v;}(),
                 [&](){Vector2 v; v.x=(float)(cx-1);  v.y=(float)cy;      return v;}(),
                 [&](){Vector2 v; v.x=(float)cx;       v.y=(float)(cy-1); return v;}(), Fade(col,0.7f));
    DrawTriangle([&](){Vector2 v; v.x=(float)(cx+rd); v.y=(float)(cy-rd); return v;}(),
                 [&](){Vector2 v; v.x=(float)cx;       v.y=(float)(cy-1); return v;}(),
                 [&](){Vector2 v; v.x=(float)(cx+1);  v.y=(float)cy;      return v;}(), Fade(col,0.7f));
    DrawTriangle([&](){Vector2 v; v.x=(float)(cx-rd); v.y=(float)(cy+rd); return v;}(),
                 [&](){Vector2 v; v.x=(float)cx;       v.y=(float)(cy+1); return v;}(),
                 [&](){Vector2 v; v.x=(float)(cx-1);  v.y=(float)cy;      return v;}(), Fade(col,0.7f));
    DrawTriangle([&](){Vector2 v; v.x=(float)(cx+rd); v.y=(float)(cy+rd); return v;}(),
                 [&](){Vector2 v; v.x=(float)(cx+1);  v.y=(float)cy;      return v;}(),
                 [&](){Vector2 v; v.x=(float)cx;       v.y=(float)(cy+1); return v;}(), Fade(col,0.7f));
    DrawRectangle(cx-2,cy-2,4,4,WHITE);
    DrawCircleLines((float)cx,(float)cy,(float)(r+3),Fade(col,0.3f));
}

static void DrawObstaclesGuide(float alpha) {
    DrawRectangle(0,0,screen_width,screen_height,Fade(MKCOLOR(8,0,0,255),alpha));
    DrawWatermark(0.07f*alpha);

    // Header
    const char *hdr="OBSTACLES  &  ENEMIES";
    DrawText(hdr, screen_width/2-MeasureText(hdr,28)/2+2, 18+2, 28, Fade(BLACK,0.6f*alpha));
    DrawText(hdr, screen_width/2-MeasureText(hdr,28)/2,   18,   28, Fade(DD_SILVER,alpha));
    DrawRectangle(screen_width/2-200,54,400,2,Fade(DD_RED,alpha));

    int iconX=68, textX=118, rowH=86, startY=70, iconSz=26;

    // Row separators
    for (int r=0;r<3;r++)
        DrawRectangle(20, startY+(r+1)*rowH-4, screen_width-40, 1,
                      Fade(MKCOLOR(60,10,10,255),alpha));

    // ── Row 0: Bouncer ────────────────────────────────────────────────────────
    DrawGuideIcon_Bouncer(iconX, startY+0*rowH+rowH/2, iconSz);
    DrawText("BOUNCER",  textX, startY+0*rowH+8,  16, Fade(MKCOLOR(255,60,60,255),alpha));
    DrawText("Moves in a straight line and bounces off walls.",
             textX, startY+0*rowH+28, 13, Fade(DD_SILVER_DIM,alpha));
    DrawText("Fully predictable — watch its rhythm and cross behind it.",
             textX, startY+0*rowH+45, 13, Fade(MKCOLOR(160,140,140,255),alpha));
    DrawText("Gets faster every level.",
             textX, startY+0*rowH+62, 12, Fade(MKCOLOR(120,100,100,255),alpha));

    // ── Row 1: Flashbanger ────────────────────────────────────────────────────
    DrawGuideIcon_Flashbanger(iconX, startY+1*rowH+rowH/2, iconSz);
    DrawText("FLASHBANGER", textX, startY+1*rowH+8, 16, Fade(MKCOLOR(255,200,0,255),alpha));
    DrawText("Bounces like a Bouncer but explodes if you get within ~4 tiles.",
             textX, startY+1*rowH+28, 13, Fade(DD_SILVER_DIM,alpha));
    DrawText("Flash = white screen + maze walls DISAPPEAR for 3 seconds!",
             textX, startY+1*rowH+45, 13, Fade(MKCOLOR(200,170,80,255),alpha));
    DrawText("Watch its pulsing danger ring and keep your distance.",
             textX, startY+1*rowH+62, 12, Fade(MKCOLOR(120,100,100,255),alpha));

    // ── Row 2: Spike Trap ─────────────────────────────────────────────────────
    DrawGuideIcon_Spike(iconX, startY+2*rowH+rowH/2, iconSz/2+4);
    DrawText("SPIKE TRAP", textX, startY+2*rowH+8, 16, Fade(MKCOLOR(255,210,10,255),alpha));
    DrawText("Stationary. Pulses in/out — only deadly when EXTENDED (bright).",
             textX, startY+2*rowH+28, 13, Fade(DD_SILVER_DIM,alpha));
    DrawText("Safe to cross when dim and small. Each spike has its own timer.",
             textX, startY+2*rowH+45, 13, Fade(MKCOLOR(160,140,140,255),alpha));
    DrawText("Look for the faint yellow floor glow to spot them early.",
             textX, startY+2*rowH+62, 12, Fade(MKCOLOR(120,100,100,255),alpha));

    // ── Controls row ─────────────────────────────────────────────────────────
    int cy2=startY+3*rowH+10;
    DrawRectangle(20,cy2-6,screen_width-40,1,Fade(DD_RED,alpha*0.5f));
    DrawText("CONTROLS", 20, cy2+2, 13, Fade(DD_RED,alpha));
    DrawText("Arrow Keys — Move     R — Restart (game over)     ESC — Home     F11 — Fullscreen",
             110, cy2+2, 13, Fade(DD_SILVER_DIM,alpha));

    DrawText("2 / 2", screen_width-52, screen_height-44, 12,
             Fade(MKCOLOR(80,70,70,255),alpha));
}

// ─── DrawSplashScreen ─────────────────────────────────────────────────────────
void DrawSplashScreen(void) {

    // ── PAGE 1: Title ─────────────────────────────────────────────────────────
    for (int f = 0; f < 180; f++) {
        if (WindowShouldClose()) return;
        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

        float progress = (float)f / 180.0f;
        float titleAlpha = (progress < 0.15f) ? (progress / 0.15f) : 1.0f;

        BeginTextureMode(gameTarget);
        ClearBackground(DD_BG);
        DrawWatermark(0.18f);
        DrawRectangle(0, 0, screen_width, 60, Fade(BLACK, 0.5f));
        DrawRectangle(0, screen_height-60, screen_width, 60, Fade(BLACK, 0.5f));
        DrawDD26Logo(680, 480, 1.8f, titleAlpha * 0.9f);
        DrawRectangle(screen_width/2-260, screen_height/2-60, 520, 90,
                      Fade(MKCOLOR(120,0,0,255), titleAlpha*0.25f));

        const char *title = "DEVDAY '26";
        int tW = MeasureText(title, 68);
        DrawText(title, screen_width/2-tW/2+3, screen_height/2-44+3, 68, Fade(BLACK,titleAlpha*0.7f));
        DrawText(title, screen_width/2-tW/2,   screen_height/2-44,   68, Fade(DD_SILVER,titleAlpha));

        const char *sub = "MAZE  CHALLENGE";
        int sW = MeasureText(sub,22);
        DrawText(sub, screen_width/2-sW/2, screen_height/2+36, 22, Fade(DD_RED_BRIGHT,titleAlpha));
        DrawRectangle(screen_width/2-160, screen_height/2+66, 320, 2, Fade(DD_RED,titleAlpha));

        // Objective line
        if (f > 50) {
            float la = fminf(1.0f,(f-50)/30.0f);
            const char *obj = "Reach the exit   E   on each level before time runs out.";
            DrawText(obj, screen_width/2-MeasureText(obj,13)/2,
                     screen_height/2+80, 13, Fade(MKCOLOR(160,140,140,255),la));
        }

        // Blinking prompt + page indicator
        if (f > 90) {
            float la = fminf(1.0f,(f-90)/20.0f);
            if ((f/15)%2==0) {
                const char *prompt = "PRESS ANY KEY  —  SEE OBSTACLES GUIDE";
                DrawText(prompt, screen_width/2-MeasureText(prompt,15)/2,
                         screen_height-52, 15, Fade(DD_SILVER_DIM,la));
            }
            DrawText("1 / 2", screen_width-52, screen_height-44, 12,
                     Fade(MKCOLOR(80,70,70,255),la));
        }

        // F11 hint
        const char *f11h = "F11  -  Toggle Fullscreen / Windowed";
        DrawText(f11h, screen_width/2-MeasureText(f11h,12)/2,
                 screen_height-22, 12, Fade(MKCOLOR(70,60,60,255),0.9f));

        PresentFrame();
        if (f > 30 && AnyGameKey()) break;
    }

    // ── PAGE 2: Obstacles guide — fade in then wait for key ───────────────────
    for (int f = 0; f < 30 && !WindowShouldClose(); f++) {
        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();
        float a = (float)f / 30.0f;
        BeginTextureMode(gameTarget);
        ClearBackground(DD_BG);
        DrawObstaclesGuide(a);
        PresentFrame();
    }
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();
        BeginTextureMode(gameTarget);
        ClearBackground(DD_BG);
        DrawObstaclesGuide(1.0f);
        if (((int)(GetTime()*2))%2==0) {
            const char *pr = "PRESS ANY KEY TO START";
            DrawText(pr, screen_width/2-MeasureText(pr,15)/2,
                     screen_height-44, 15, DD_SILVER_DIM);
        }
        PresentFrame();
        if (AnyGameKey()) break;
    }
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main(void) {
    // Opens windowed by default; F11 toggles fullscreen at any time
    InitWindow(screen_width, screen_height, "DEVDAY '26  -  Maze Challenge");
    SetTargetFPS(60);

    gameTarget = LoadRenderTexture(screen_width, screen_height);
    SetTextureFilter(gameTarget.texture, TEXTURE_FILTER_BILINEAR);

    InitAudioDevice();
    InitSounds();

    PLAYER player;
    player.sprite   = LoadTexture("player.png");
    player.position = [&](){Vector2 _v; _v.x=(float)tile_size; _v.y=(float)tile_size; return _v;}();
    player.speed    = 2.0f;

    // ── Login → outer home loop ───────────────────────────────────────────────
    DrawLoginScreen();
    if (WindowShouldClose()) goto cleanup;

    while (!WindowShouldClose()) {

        // Reset all state for a fresh play session
        time_left=80.0f; max_time=80.0f;
        current_level=0; lives=3;
        game_over=false; player_won=false;
        ran_out_of_time=false; loss_display_frames=0;
        invincibility_frames=0; flashbang_frames=0; flashbang_white=0.0f;
        total_score=0;
        resetplayer(&player); InitEnemies(); InitSpikes();

        DrawSplashScreen();
        if (WindowShouldClose()) break;

        session_start = GetTime();
        bool score_submitted       = false;
        int  submit_display_frames = 0;
        bool go_home               = false;   // ESC on game-over → home screen

    // ── Inner game loop ───────────────────────────────────────────────────────
    while (!WindowShouldClose() && !go_home) {

        // F11 — toggle fullscreen / windowed at any time
        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

        if (!game_over) {
            UpdateTimeandcheckgameover();
            UpdatePlayer(&player);
            UpdateEnemies(&player);
            UpdateSpikes(&player);
        } else {
            // First game-over frame: submit score
            if (!score_submitted) {
                score_submitted = true;
                if (!guest_mode && !player_code.empty()) {
                    float play_time = (float)(GetTime() - session_start);
                    total_score = CalcScore();
                    AsyncSubmitScore(player_code, total_score, play_time);
                    submit_display_frames = 180;
                }
            }
            if (submit_display_frames > 0) submit_display_frames--;

            if (IsKeyPressed(KEY_R)) {
                time_left=80.0f; max_time=80.0f;
                current_level=0; lives=3;
                game_over=false; player_won=false;
                ran_out_of_time=false; loss_display_frames=0;
                invincibility_frames=0; flashbang_frames=0; flashbang_white=0.0f;
                score_submitted=false; submit_display_frames=0;
                submit_done=false; total_score=0;
                session_start=GetTime();
                resetplayer(&player); InitEnemies(); InitSpikes();
            }
            // ESC on game-over → return to home/splash screen
            if (loss_display_frames <= 0 && IsKeyPressed(KEY_ESCAPE))
                go_home = true;
        }

        BeginTextureMode(gameTarget);
        ClearBackground(DD_BG);
        DrawWatermark(0.06f);
        DrawMaze(); DrawSpikes(); DrawEnemies(); DrawPlayer(&player);

        if (flashbang_white > 0.0f)
            DrawRectangle(0, 0, screen_width, screen_height, Fade(WHITE, flashbang_white));

        if (loss_display_frames > 0) {
            DrawRectangle(0,0,screen_width,screen_height,Fade(BLACK,0.82f));
            DrawWatermark(0.12f);
            const char *t1 = ran_out_of_time ? "TIME'S UP!" : "NO LIVES LEFT!";
            DrawText(t1, screen_width/2-MeasureText(t1,62)/2+3, screen_height/2-55+3, 62, Fade(BLACK,0.6f));
            DrawText(t1, screen_width/2-MeasureText(t1,62)/2,   screen_height/2-55,   62, DD_RED_BRIGHT);
            const char *t2 = ran_out_of_time ? "YOU  LOSE" : "YOU WERE DEFEATED";
            DrawText(t2, screen_width/2-MeasureText(t2,38)/2, screen_height/2+20, 38, DD_SILVER_DIM);
            DrawDD26Logo(screen_width/2, screen_height/2+110, 0.7f, 0.7f);
            loss_display_frames--;

        } else if (game_over) {
            DrawRectangle(0,0,screen_width,screen_height,Fade(BLACK,0.86f));
            DrawWatermark(0.12f);
            DrawDD26Logo(screen_width-90, screen_height-100, 1.1f, 0.5f);

            if (player_won) {
                const char *w1 = "ALL LEVELS CLEARED!";
                DrawText(w1, screen_width/2-MeasureText(w1,42)/2+2, screen_height/2-65+2, 42, Fade(BLACK,0.5f));
                DrawText(w1, screen_width/2-MeasureText(w1,42)/2,   screen_height/2-65,   42, DD_SILVER);

                // Show player name on win
                if (!guest_mode) {
                    char nameline[64];
                    snprintf(nameline, sizeof(nameline), "Congratulations, %s!", player_name.c_str());
                    DrawText(nameline, screen_width/2-MeasureText(nameline,18)/2,
                             screen_height/2-14, 18, DD_YELLOW);
                }

                const char *w2 = "DEVDAY '26  -  MAZE CHAMPION";
                DrawText(w2, screen_width/2-MeasureText(w2,22)/2, screen_height/2+10, 22, DD_RED_BRIGHT);
                DrawRectangle(screen_width/2-160, screen_height/2+40, 320, 2, DD_RED);

                // Score display
                char scoreStr[32];
                snprintf(scoreStr, sizeof(scoreStr), "SCORE: %d", total_score);
                DrawText(scoreStr, screen_width/2-MeasureText(scoreStr,24)/2,
                         screen_height/2+50, 24, DD_SILVER);

            } else {
                const char *g1 = "GAME  OVER";
                DrawText(g1, screen_width/2-MeasureText(g1,58)/2+2, screen_height/2-55+2, 58, Fade(BLACK,0.5f));
                DrawText(g1, screen_width/2-MeasureText(g1,58)/2,   screen_height/2-55,   58, DD_RED_BRIGHT);
                const char *g2 = ran_out_of_time ? "TIME EXPIRED" : "YOU RAN OUT OF LIVES";
                DrawText(g2, screen_width/2-MeasureText(g2,18)/2, screen_height/2+10, 18, DD_SILVER_DIM);
            }

            // ── Score submit status (non-guest) ───────────────────────────────
            if (!guest_mode && submit_display_frames > 0) {
                Color sc = submit_done ? (submit_ok ? DD_GREEN : DD_RED_BRIGHT) : DD_YELLOW;
                const char *smsg = submit_done
                    ? submit_result_msg.c_str()
                    : "Submitting score...";
                DrawText(smsg, screen_width/2-MeasureText(smsg,14)/2,
                         screen_height/2+75, 14, sc);
            }
            if (guest_mode) {
                DrawText("Guest mode — score not saved.",
                         screen_width/2-MeasureText("Guest mode — score not saved.",13)/2,
                         screen_height/2+75, 13, DD_SILVER_DIM);
            }

            const char *r1 = "PRESS  R  TO  RESTART";
            const char *r2 = "PRESS  ESC  TO  HOME";
            DrawText(r1, screen_width/2-MeasureText(r1,18)/2, screen_height/2+100, 18, DD_SILVER_DIM);
            DrawText(r2, screen_width/2-MeasureText(r2,18)/2, screen_height/2+128, 18, MKCOLOR(80,75,75,255));

        } else {
            DrawHUD();
        }

        PresentFrame();
    }   // ── end inner game loop

    }   // ── end outer home loop

cleanup:
    UnloadRenderTexture(gameTarget);
    UnloadSound(snd_hit); UnloadSound(snd_bang);
    UnloadSound(snd_levelup); UnloadSound(snd_gameover);
    CloseAudioDevice();
    UnloadTexture(player.sprite);
    CloseWindow();
    return 0;
}

// ─── IsTileBlocked ────────────────────────────────────────────────────────────
bool IsTileBlocked(int tx, int ty) {
    if (tx < 0 || tx >= map_width || ty < 0 || ty >= map_height) return true;
    int t = levels[current_level][ty][tx];
    return (t == 1 || t == 2);
}

// ─── InitEnemies ──────────────────────────────────────────────────────────────
void InitEnemies(void) {
    float data[num_levels][MAX_ENEMIES][6] = {
        {{ 2,1,1.4f, 1,0,ENEMY_BOUNCER},{ 36,5,1.4f,-1,0,ENEMY_BOUNCER},
         { 5,15,0.9f,1,0,ENEMY_CHASER},{20,15,0.9f,-1,0,ENEMY_CHASER},
         { 3,27,1.1f,0,-1,ENEMY_FLASHBANGER},{37,3,1.1f,0,1,ENEMY_FLASHBANGER}},
        {{ 2,1,1.8f, 1,0,ENEMY_BOUNCER},{ 36,5,1.8f,-1,0,ENEMY_BOUNCER},
         { 5,15,1.2f,1,0,ENEMY_CHASER},{20,15,1.2f,-1,0,ENEMY_CHASER},
         { 3,27,1.4f,0,-1,ENEMY_FLASHBANGER},{37,3,1.4f,0,1,ENEMY_FLASHBANGER}},
        {{ 2,1,2.3f, 1,0,ENEMY_BOUNCER},{ 36,5,2.3f,-1,0,ENEMY_BOUNCER},
         { 5,15,1.6f,1,0,ENEMY_CHASER},{20,15,1.6f,-1,0,ENEMY_CHASER},
         { 3,27,1.8f,0,-1,ENEMY_FLASHBANGER},{37,3,1.8f,0,1,ENEMY_FLASHBANGER}}
    };
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].position = [&](){Vector2 _v; _v.x=data[current_level][i][0]*tile_size; _v.y=data[current_level][i][1]*tile_size; return _v;}();
        enemies[i].speed = data[current_level][i][2];
        enemies[i].dirX  = data[current_level][i][3];
        enemies[i].dirY  = data[current_level][i][4];
        enemies[i].type  = (int)data[current_level][i][5];
        // Blue Ghost Chasers removed to reduce difficulty
        enemies[i].active= (enemies[i].type != ENEMY_CHASER);
    }
}

// ─── InitSpikes ───────────────────────────────────────────────────────────────
void InitSpikes(void) {
    srand((unsigned int)(current_level * 7919 + 42));
    int placed = 0;
    for (int attempts = 0; attempts < 2000 && placed < MAX_SPIKES; attempts++) {
        int tx = 2 + rand() % (map_width  - 4);
        int ty = 2 + rand() % (map_height - 4);
        if (levels[current_level][ty][tx] == 0) {
            int dx = tx - start_tile_x, dy = ty - start_tile_y;
            if (dx*dx + dy*dy > 36) {
                bool overlap = false;
                for (int s = 0; s < placed; s++)
                    if (abs(spikes[s].tx-tx)+abs(spikes[s].ty-ty) < 3)
                        { overlap = true; break; }
                if (!overlap) {
                    spikes[placed] = { tx, ty, true, (float)placed * 0.9f };
                    placed++;
                }
            }
        }
    }
    for (int i = placed; i < MAX_SPIKES; i++) spikes[i].active = false;
}

// ─── UpdateEnemies ────────────────────────────────────────────────────────────
void UpdateEnemies(PLAYER *player) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;

        if (enemies[i].type == ENEMY_BOUNCER) {
            Vector2 np = { enemies[i].position.x + enemies[i].dirX * enemies[i].speed,
                           enemies[i].position.y + enemies[i].dirY * enemies[i].speed };
            int tx1=(int)(np.x/tile_size), ty1=(int)(np.y/tile_size);
            int tx2=(int)((np.x+tile_size-1)/tile_size), ty2=(int)((np.y+tile_size-1)/tile_size);
            if (IsTileBlocked(tx1,ty1)||IsTileBlocked(tx2,ty1)||
                IsTileBlocked(tx1,ty2)||IsTileBlocked(tx2,ty2))
                { enemies[i].dirX=-enemies[i].dirX; enemies[i].dirY=-enemies[i].dirY; }
            else enemies[i].position = np;
        }
        else if (enemies[i].type == ENEMY_CHASER) {
            float pdx=player->position.x-enemies[i].position.x;
            float pdy=player->position.y-enemies[i].position.y;
            float dist=sqrtf(pdx*pdx+pdy*pdy);
            if (dist>0.1f) {
                enemies[i].dirX+=(pdx/dist)*0.06f; enemies[i].dirY+=(pdy/dist)*0.06f;
                float dl=sqrtf(enemies[i].dirX*enemies[i].dirX+enemies[i].dirY*enemies[i].dirY);
                if (dl>0.01f) { enemies[i].dirX/=dl; enemies[i].dirY/=dl; }
            }
            Vector2 np = { enemies[i].position.x+enemies[i].dirX*enemies[i].speed,
                           enemies[i].position.y+enemies[i].dirY*enemies[i].speed };
            int tx1=(int)(np.x/tile_size), ty1=(int)(np.y/tile_size);
            int tx2=(int)((np.x+tile_size-1)/tile_size), ty2=(int)((np.y+tile_size-1)/tile_size);
            if (IsTileBlocked(tx1,ty1)||IsTileBlocked(tx2,ty1)||
                IsTileBlocked(tx1,ty2)||IsTileBlocked(tx2,ty2)) {
                enemies[i].dirX=-enemies[i].dirX+((float)(rand()%3-1))*0.6f;
                enemies[i].dirY=-enemies[i].dirY+((float)(rand()%3-1))*0.6f;
                float dl=sqrtf(enemies[i].dirX*enemies[i].dirX+enemies[i].dirY*enemies[i].dirY);
                if (dl>0.01f){enemies[i].dirX/=dl;enemies[i].dirY/=dl;}
            } else enemies[i].position=np;
        }
        else if (enemies[i].type == ENEMY_FLASHBANGER) {
            Vector2 np = { enemies[i].position.x+enemies[i].dirX*enemies[i].speed,
                           enemies[i].position.y+enemies[i].dirY*enemies[i].speed };
            int tx1=(int)(np.x/tile_size), ty1=(int)(np.y/tile_size);
            int tx2=(int)((np.x+tile_size-1)/tile_size), ty2=(int)((np.y+tile_size-1)/tile_size);
            if (IsTileBlocked(tx1,ty1)||IsTileBlocked(tx2,ty1)||
                IsTileBlocked(tx1,ty2)||IsTileBlocked(tx2,ty2))
                { enemies[i].dirX=-enemies[i].dirX; enemies[i].dirY=-enemies[i].dirY; }
            else enemies[i].position=np;
            if (flashbang_frames==0) {
                float pdx=player->position.x-enemies[i].position.x;
                float pdy=player->position.y-enemies[i].position.y;
                if (sqrtf(pdx*pdx+pdy*pdy) < tile_size*4.5f)
                    { flashbang_frames=FLASHBANG_DURATION; flashbang_white=1.0f; PlaySound(snd_bang); }
            }
        }

        if (invincibility_frames>0) continue;
        float dx=player->position.x-enemies[i].position.x;
        float dy=player->position.y-enemies[i].position.y;
        if (sqrtf(dx*dx+dy*dy) < tile_size*0.85f) {
            lives--; invincibility_frames=INVINCIBILITY_DURATION; PlaySound(snd_hit);
            if (lives<=0) { lives=0; ran_out_of_time=false; loss_display_frames=90;
                            game_over=true; PlaySound(snd_gameover); }
            else resetplayer(player);
        }
    }
    if (invincibility_frames>0) invincibility_frames--;
    if (flashbang_frames>0) {
        flashbang_frames--;
        flashbang_white-=0.04f;
        if (flashbang_white<0.0f) flashbang_white=0.0f;
    }
}

// ─── UpdateSpikes ─────────────────────────────────────────────────────────────
void UpdateSpikes(PLAYER *player) {
    if (invincibility_frames>0) return;
    int ptx=(int)((player->position.x+tile_size*0.5f)/tile_size);
    int pty=(int)((player->position.y+tile_size*0.5f)/tile_size);
    for (int i=0;i<MAX_SPIKES;i++) {
        if (!spikes[i].active) continue;
        if (ptx==spikes[i].tx && pty==spikes[i].ty) {
            float t=(float)GetTime()*2.5f+spikes[i].anim;
            if (sinf(t)>0.2f) {
                lives--; invincibility_frames=INVINCIBILITY_DURATION; PlaySound(snd_hit);
                if (lives<=0) { lives=0; ran_out_of_time=false; loss_display_frames=90;
                                game_over=true; PlaySound(snd_gameover); }
                else resetplayer(player);
            }
        }
    }
}

// ─── DrawEnemies ──────────────────────────────────────────────────────────────
void DrawEnemies(void) {
    for (int i=0;i<MAX_ENEMIES;i++) {
        if (!enemies[i].active) continue;
        int px=(int)enemies[i].position.x, py=(int)enemies[i].position.y;
        if (enemies[i].type==ENEMY_BOUNCER) {
            DrawRectangle(px,py,tile_size,tile_size,MKCOLOR(60,5,5,255));
            DrawRectangle(px+2,py+2,tile_size-4,tile_size-4,MKCOLOR(200,15,15,255));
            DrawRectangle(px+3,py+5,5,5,WHITE); DrawRectangle(px+11,py+5,5,5,WHITE);
            DrawRectangle(px+4,py+6,3,3,MKCOLOR(180,0,0,255));
            DrawRectangle(px+12,py+6,3,3,MKCOLOR(180,0,0,255));
            DrawRectangle(px+2,py+2,tile_size-4,2,MKCOLOR(255,60,60,255));
        } else if (enemies[i].type==ENEMY_CHASER) {
            float wave=sinf((float)GetTime()*3.5f+i*1.3f);
            unsigned char bodyA=(unsigned char)(160+60*wave);
            DrawRectangle(px,py,tile_size,tile_size,MKCOLOR(5,15,60,220));
            DrawRectangle(px+2,py+2,tile_size-4,tile_size-4,MKCOLOR(30,80,220,bodyA));
            for (int d=0;d<3;d++) {
                int dx2=px+3+d*6;
                int dropH=3+(int)(2*sinf((float)GetTime()*4+d*1.1f+i));
                DrawRectangle(dx2,py+tile_size-1,3,dropH,MKCOLOR(30,80,220,120));
            }
            DrawRectangle(px+3,py+5,4,4,WHITE); DrawRectangle(px+11,py+5,4,4,WHITE);
            DrawRectangle(px+4,py+6,2,2,MKCOLOR(0,0,180,255));
            DrawRectangle(px+12,py+6,2,2,MKCOLOR(0,0,180,255));
            DrawRectangle(px+2,py+2,tile_size-4,2,MKCOLOR(100,160,255,255));
        } else if (enemies[i].type==ENEMY_FLASHBANGER) {
            float pulse=(sinf((float)GetTime()*7.0f+i*0.7f)+1.0f)*0.5f;
            unsigned char g=(unsigned char)(160+95*pulse);
            Color col=MKCOLOR(255,g,0,255), dimCol=MKCOLOR(80,(unsigned char)(40+30*pulse),0,255);
            DrawRectangle(px,py,tile_size,tile_size,dimCol);
            DrawRectangle(px+tile_size/2-2,py+2,4,tile_size-4,col);
            DrawRectangle(px+2,py+tile_size/2-2,tile_size-4,4,col);
            DrawRectangle(px+tile_size/2-3,py+tile_size/2-3,6,6,WHITE);
            DrawCircleLines(px+tile_size/2,py+tile_size/2,tile_size*(3.5f+1.0f*pulse),Fade(col,0.25f));
            DrawCircleLines(px+tile_size/2,py+tile_size/2,tile_size*0.8f,Fade(col,0.6f));
        }
    }
}

// ─── DrawSpikes ───────────────────────────────────────────────────────────────
void DrawSpikes(void) {
    for (int i=0;i<MAX_SPIKES;i++) {
        if (!spikes[i].active) continue;
        int px=spikes[i].tx*tile_size, py=spikes[i].ty*tile_size;
        float t=(float)GetTime()*2.5f+spikes[i].anim;
        float pulse=(sinf(t)+1.0f)*0.5f;
        bool deadly=sinf(t)>0.2f;
        Color col=deadly?MKCOLOR(255,210,10,255):MKCOLOR(70,55,5,255);
        Color glowCol=Fade(col,deadly?0.35f:0.08f);
        int cx=px+tile_size/2, cy=py+tile_size/2, r=4+(int)(5.0f*pulse);
        DrawRectangle(px,py,tile_size,tile_size,Fade(col,0.12f));
        DrawTriangle([&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy-r); return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)(cx-2); _v.y=(float)cy; return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)(cx+2); _v.y=(float)cy; return _v;}(), col);
        DrawTriangle([&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy+r); return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)(cx+2); _v.y=(float)cy; return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)(cx-2); _v.y=(float)cy; return _v;}(), col);
        DrawTriangle([&](){Vector2 _v; _v.x=(float)(cx-r); _v.y=(float)cy; return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy-2); return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy+2); return _v;}(), col);
        DrawTriangle([&](){Vector2 _v; _v.x=(float)(cx+r); _v.y=(float)cy; return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy+2); return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy-2); return _v;}(), col);
        int rd=r*6/10;
        DrawTriangle([&](){Vector2 _v; _v.x=(float)(cx-rd); _v.y=(float)(cy-rd); return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)(cx-1); _v.y=(float)cy; return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy-1); return _v;}(), Fade(col,0.7f));
        DrawTriangle([&](){Vector2 _v; _v.x=(float)(cx+rd); _v.y=(float)(cy-rd); return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy-1); return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)(cx+1); _v.y=(float)cy; return _v;}(), Fade(col,0.7f));
        DrawTriangle([&](){Vector2 _v; _v.x=(float)(cx-rd); _v.y=(float)(cy+rd); return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy+1); return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)(cx-1); _v.y=(float)cy; return _v;}(), Fade(col,0.7f));
        DrawTriangle([&](){Vector2 _v; _v.x=(float)(cx+rd); _v.y=(float)(cy+rd); return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)(cx+1); _v.y=(float)cy; return _v;}(),
                     [&](){Vector2 _v; _v.x=(float)cx; _v.y=(float)(cy+1); return _v;}(), Fade(col,0.7f));
        DrawRectangle(cx-2,cy-2,4,4,WHITE);
        if (deadly) DrawCircleLines((float)cx,(float)cy,(float)(r+3),glowCol);
    }
}

// ─── DrawHUD ──────────────────────────────────────────────────────────────────
void DrawHUD(void) {
    const int HUD_H=28;
    DrawRectangle(0,0,screen_width,HUD_H,DD_HUD_BG);
    DrawRectangle(0,HUD_H-2,screen_width,2,DD_RED);
    DrawText("DD26",8,6,14,DD_RED);
    DrawText("|",42,5,18,MKCOLOR(60,15,15,255));
    DrawText(TextFormat("LEVEL %d / %d",current_level+1,num_levels),52,7,15,DD_SILVER);
    // F11 mode hint
    const char *fsHint = IsWindowFullscreen() ? "F11: Windowed" : "F11: Fullscreen";
    DrawText(fsHint, screen_width/2-300, 8, 12, MKCOLOR(70,60,60,255));

    // Player name / guest tag (right of level)
    if (!player_name.empty()) {
        const char *nm = guest_mode
            ? "GUEST"
            : player_name.c_str();
        Color nc = guest_mode ? DD_SILVER_DIM : DD_YELLOW;
        DrawText(nm, 200, 7, 13, nc);
    }

    float ratio=time_left/max_time; if(ratio<0)ratio=0;
    Color barCol=(ratio>0.5f)?MKCOLOR(30,180,50,255):
                 (ratio>0.25f)?MKCOLOR(200,100,10,255):MKCOLOR(200,15,15,255);
    const int BW=220,BH=16,BX=screen_width/2-BW/2,BY=6;
    DrawRectangle(BX-1,BY-1,BW+2,BH+2,DD_RED);
    DrawRectangle(BX,BY,BW,BH,MKCOLOR(20,3,3,255));
    DrawRectangle(BX,BY,(int)(BW*ratio),BH,barCol);
    const char *ts=TextFormat("%.0fs",time_left);
    DrawText(ts,BX+BW/2-MeasureText(ts,14)/2,BY+1,14,WHITE);

    if (flashbang_frames>0) {
        float fb=(float)flashbang_frames/FLASHBANG_DURATION;
        DrawRectangle(BX,BY+BH+3,(int)(BW*fb),3,MKCOLOR(255,200,0,200));
    }

    DrawText("LIVES",screen_width-140,7,13,DD_SILVER_DIM);
    DrawText(TextFormat("%d / 3",lives),screen_width-124,16,11,DD_SILVER);
    for (int i=0;i<3;i++) {
        int lx=screen_width-78+i*24, ly=6;
        if (i<lives) {
            DrawTriangle([&](){Vector2 _v; _v.x=(float)(lx+8); _v.y=(float)ly; return _v;}(),
                         [&](){Vector2 _v; _v.x=(float)(lx+16); _v.y=(float)(ly+8); return _v;}(),
                         [&](){Vector2 _v; _v.x=(float)(lx+8); _v.y=(float)(ly+16); return _v;}(),DD_RED);
            DrawTriangle([&](){Vector2 _v; _v.x=(float)lx; _v.y=(float)(ly+8); return _v;}(),
                         [&](){Vector2 _v; _v.x=(float)(lx+8); _v.y=(float)ly; return _v;}(),
                         [&](){Vector2 _v; _v.x=(float)(lx+8); _v.y=(float)(ly+16); return _v;}(),MKCOLOR(140,10,10,255));
        } else {
            DrawTriangle([&](){Vector2 _v; _v.x=(float)(lx+8); _v.y=(float)ly; return _v;}(),
                         [&](){Vector2 _v; _v.x=(float)(lx+16); _v.y=(float)(ly+8); return _v;}(),
                         [&](){Vector2 _v; _v.x=(float)(lx+8); _v.y=(float)(ly+16); return _v;}(),MKCOLOR(40,10,10,255));
        }
    }
}

// ─── UpdateTimeandcheckgameover ───────────────────────────────────────────────
void UpdateTimeandcheckgameover(void) {
    time_left-=GetFrameTime();
    if (time_left<=0) {
        time_left=0; ran_out_of_time=true;
        loss_display_frames=90; game_over=true; PlaySound(snd_gameover);
    }
}

// ─── DrawMaze ─────────────────────────────────────────────────────────────────
void DrawMaze(void) {
    float pulse=(sinf((float)GetTime()*4.0f)+1.0f)*0.5f;
    Color exitColor={(unsigned char)(200+55*pulse),(unsigned char)(30*pulse),(unsigned char)(30*pulse),255};
    bool blinded=(flashbang_frames>0);
    for (int y=0;y<map_height;y++) for (int x=0;x<map_width;x++) {
        int tile=levels[current_level][y][x];
        int px=x*tile_size, py=y*tile_size;
        if (tile==0) DrawRectangle(px,py,tile_size,tile_size,DD_FLOOR);
        else if (tile==1) {
            if (blinded) DrawRectangle(px,py,tile_size,tile_size,DD_FLOOR);
            else { DrawRectangle(px,py,tile_size,tile_size,DD_WALL);
                   DrawRectangle(px,py,tile_size,2,DD_WALL_H); }
        } else if (tile==2) {
            if (blinded) DrawRectangle(px,py,tile_size,tile_size,MKCOLOR(40,8,8,255));
            else { DrawRectangle(px,py,tile_size,tile_size,DD_BORDER);
                   DrawRectangle(px,py,tile_size,2,DD_BORDER_H); }
        } else if (tile==3) {
            DrawRectangle(px,py,tile_size,tile_size,exitColor);
            DrawText("E",px+6,py+4,12,WHITE);
        }
    }
}

// ─── DrawPlayer ───────────────────────────────────────────────────────────────
void DrawPlayer(PLAYER *player) {
    if (invincibility_frames>0 && (invincibility_frames/6)%2==0) return;
    float scale=(float)tile_size/(float)player->sprite.width;
    DrawTextureEx(player->sprite, player->position, 0.0f, scale, WHITE);
}

// ─── UpdatePlayer ─────────────────────────────────────────────────────────────
void UpdatePlayer(PLAYER *player) {
    Vector2 pm={0,0};
    if (IsKeyDown(KEY_UP))    pm.y-=player->speed;
    if (IsKeyDown(KEY_DOWN))  pm.y+=player->speed;
    if (IsKeyDown(KEY_LEFT))  pm.x-=player->speed;
    if (IsKeyDown(KEY_RIGHT)) pm.x+=player->speed;

    if (pm.x!=0) {
        Vector2 np={player->position.x+pm.x, player->position.y};
        int cX=(int)((np.x+tile_size/2.0f+(pm.x>0?1.0f:-1.0f))/tile_size);
        int cY=(int)((np.y+tile_size/2.0f)/tile_size);
        int cX1=(int)((np.x+2)/tile_size), cX2=(int)((np.x+tile_size-2)/tile_size);
        int cY1=(int)((np.y+4)/tile_size), cY2=(int)((np.y+tile_size-2)/tile_size);
        if (pm.x<0) {
            if ((levels[current_level][cY][cX]!=1&&levels[current_level][cY1][cX1]!=1&&levels[current_level][cY2][cX1]!=1)&&
                (levels[current_level][cY][cX]!=2&&levels[current_level][cY1][cX1]!=2&&levels[current_level][cY2][cX1]!=2))
                player->position.x=np.x;
        } else {
            if ((levels[current_level][cY][cX]!=1&&levels[current_level][cY1][cX2]!=1&&levels[current_level][cY2][cX2]!=1)&&
                (levels[current_level][cY][cX]!=2&&levels[current_level][cY1][cX2]!=2&&levels[current_level][cY2][cX2]!=2))
                player->position.x=np.x;
        }
    }
    if (pm.y!=0) {
        Vector2 np={player->position.x, player->position.y+pm.y};
        int cX=(int)((np.x+tile_size/2.0)/tile_size);
        int cY=(int)((np.y+tile_size/2.0+(pm.y>0?1.0:-1.0))/tile_size);
        int cX1=(int)((np.x+2)/tile_size), cX2=(int)((np.x+tile_size-2)/tile_size);
        int cY1=(int)((np.y+4)/tile_size), cY2=(int)((np.y+tile_size+2)/tile_size);
        if (pm.y<0) {
            if ((levels[current_level][cY][cX]!=1&&levels[current_level][cY1][cX1]!=1&&levels[current_level][cY1][cX2]!=1)&&
                (levels[current_level][cY][cX]!=2&&levels[current_level][cY1][cX1]!=2&&levels[current_level][cY1][cX2]!=2))
                player->position.y=np.y;
        } else {
            if ((levels[current_level][cY][cX]!=1&&levels[current_level][cY2][cX1]!=1&&levels[current_level][cY2][cX2]!=1)&&
                (levels[current_level][cY][cX]!=2&&levels[current_level][cY2][cX1]!=2&&levels[current_level][cY2][cX2]!=2))
                player->position.y=np.y;
        }
    }
    Checkwincondition(player);
}

// ─── resetplayer ──────────────────────────────────────────────────────────────
void resetplayer(PLAYER *player) {
    player->position=[&](){Vector2 _v; _v.x=(float)start_tile_x*tile_size; _v.y=(float)start_tile_y*tile_size; return _v;}();
}

// ─── Checkwincondition ────────────────────────────────────────────────────────
void Checkwincondition(PLAYER *player) {
    int mapX=(int)((player->position.x+tile_size/2.0f)/tile_size);
    int mapY=(int)((player->position.y+tile_size/2.0f)/tile_size);
    if (mapX<0||mapX>=map_width||mapY<0||mapY>=map_height) return;
    if (levels[current_level][mapY][mapX]!=3) return;

    PlaySound(snd_levelup);

    for (int f=0;f<100;f++) {
        float prog=(float)f/100.0f;
        BeginTextureMode(gameTarget);
        ClearBackground(DD_BG);
        DrawWatermark(0.10f);
        DrawMaze(); DrawSpikes(); DrawEnemies(); DrawPlayer(player);
        DrawRectangle(0,0,screen_width,screen_height,Fade(BLACK,0.80f));
        DrawDD26Logo(screen_width-80,screen_height-80,1.0f,prog*0.6f);
        const char *cl=TextFormat("LEVEL %d  CLEARED!",current_level+1);
        DrawText(cl,screen_width/2-MeasureText(cl,48)/2+2,screen_height/2-40+2,48,Fade(BLACK,0.5f));
        DrawText(cl,screen_width/2-MeasureText(cl,48)/2,  screen_height/2-40,  48,DD_SILVER);
        DrawRectangle(screen_width/2-150,screen_height/2+18,300,2,DD_RED);
        DrawText("Get ready...",screen_width/2-MeasureText("Get ready...",20)/2,
                 screen_height/2+30,20,DD_RED_BRIGHT);
        DrawText("DEVDAY '26  -  MAZE CHALLENGE",
                 screen_width/2-MeasureText("DEVDAY '26  -  MAZE CHALLENGE",14)/2,
                 screen_height-30,14,MKCOLOR(80,20,20,255));
        PresentFrame();
    }

    // Add time-bonus score for completing a level
    total_score += (int)(time_left * 10) + lives * 100;

    current_level++;
    if (current_level>=num_levels) { player_won=true; game_over=true; return; }
    if (current_level==1) { time_left=65.0f; max_time=65.0f; }
    else                  { time_left=75.0f; max_time=75.0f; }
    invincibility_frames=0; flashbang_frames=0; flashbang_white=0.0f;
    resetplayer(player); InitEnemies(); InitSpikes();
}