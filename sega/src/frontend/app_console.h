#pragma once

// What the window's main loop (common/src/frontend/main.cpp) needs to know of plumsg's
// console before anything else: which it is, how it opens a game, keeps its save and
// takes its cheats, and plumsg's own command-line options.
#include <memory>
#include <string>
#include <vector>

#include "../core/cheats.h"
#include "../core/megadrive.h"
#include "core/cheat_list.h"

namespace plumsg {

// The console the window runs: plumsg has one, always there, which every game goes into.
using GameConsole = MegaDrive;

// The console's picture at 224 lines is this wide at 4:3 (before the widescreen columns),
// and a recording of its own picture this wide (H40: an H32 picture is stretched to it,
// as on a TV).
constexpr int kConsoleWidth = 320;
constexpr int kRecordWidth = kMdMaxWidth;
// --filter takes lcd; the options and their values in Spanish of the versions up to 0.0.31
// (--pantalla-completa, --region japon...) still work.
constexpr bool kLcdFilter = false;
constexpr bool kSpanishOptions = true;

// plumsg's own command-line options.
struct AppCommandLine {
    bool bios = false;    // --bios: start with the Mega CD BIOS window open
    bool editor = false;  // --instruments: start with the instrument editor open
    std::string track;    // --track f.vgm: and this track in it
    double seek = -1;     // --seek s: and at that second of it, playing
    bool score = false;   // --score: start with the Partitura open
    // Takes argv[i] if it is one of these (and its value, moving i past it).
    bool parse(const std::string& a, int& i, int argc, char** argv);
    // A test pattern instead of a game (a ConsoleSystem); plumsg has none.
    int testPattern() const { return -1; }
};
extern const char* const kHelp;

// The console before any game: plumsg's is made once, at the start.
void newConsole(std::unique_ptr<GameConsole>& console);
// A game into the console (`test` is never set here); false with the reason.
bool openConsole(std::unique_ptr<GameConsole>& console, const std::string& path, int test, std::string* error);
// The console after the game is closed.
void closeConsole(std::unique_ptr<GameConsole>& console);
// The game's name as its header gives it.
std::string consoleTitle(GameConsole& console);
// The codes that are on and valid, as the console takes them.
std::vector<CheatPatch> decodeCheats(GameConsole& console, const std::vector<Cheat>& cheats);
// Cartridge SRAM, or the Mega CD's backup RAM, in `path`.
void saveBattery(GameConsole& console, const std::string& path);
void loadBattery(GameConsole& console, const std::string& path);

}  // namespace plumsg
