#pragma once

// What the window's main loop (common/src/frontend/main.cpp) needs to know of plumnin's
// consoles before anything else: which they are, how a game opens one, keeps its save and
// takes its cheats, and plumnin's own command-line options.
#include <memory>
#include <string>
#include <vector>

#include "../core/console.h"
#include "core/cheat_list.h"

namespace plumnin {

// The console the window runs: the game's own (a Game Boy, a NES...), made when it opens.
using GameConsole = Console;

// The widest console's picture at 224 lines is this wide at 4:3 (before the widescreen
// columns), and a recording of the console's own picture is as wide as the widest.
constexpr int kConsoleWidth = 256;
constexpr int kRecordWidth = VideoFrame::kMaxWidth;
// --filter takes lcd (the Game Boy's screen); no options in Spanish from older versions.
constexpr bool kLcdFilter = true;
constexpr bool kSpanishOptions = false;

// plumnin's own command-line options.
struct AppCommandLine {
    bool instruments = false;  // --instruments: start with the instrument editor open
    int test = -1;             // --test-pattern [gb|gbc|nes|snes]: the test pattern instead of a game (a ConsoleSystem)
    // Takes argv[i] if it is one of these (and its value, moving i past it).
    bool parse(const std::string& a, int& i, int argc, char** argv);
    int testPattern() const { return test; }
};
extern const char* const kHelp;

// The console before any game: none, until a game makes one.
inline void newConsole(std::unique_ptr<GameConsole>&) {}
// The console for that game (or the test pattern of console `test`), into `console` if it
// opens; false with the reason.
bool openConsole(std::unique_ptr<GameConsole>& console, const std::string& path, int test, std::string* error);
// The console after the game is closed: gone with it.
void closeConsole(std::unique_ptr<GameConsole>& console);
std::string consoleTitle(GameConsole& console);
// The codes that are on and valid, as the console takes them.
std::vector<CheatPatch> decodeCheats(GameConsole& console, const std::vector<Cheat>& cheats);
// The cartridge's battery-backed memory, in `path`.
void saveBattery(GameConsole& console, const std::string& path);
void loadBattery(GameConsole& console, const std::string& path);

}  // namespace plumnin
