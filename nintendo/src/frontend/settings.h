#pragma once

#include <array>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "../core/pad.h"
#include "../core/system.h"
#include "render/renderer.h"
#include "frontend/input.h"
#include "frontend/settings_base.h"

namespace plumnin {

// Everything the menus change apart from the controls. Lives in plumnin.ini next to
// the [keyboard] / [gamepad] sections, under [interface], [video], [audio], [console],
// [speed], [states], [library], [network], [achievements], [screenscraper], [recent], [history]
// and [favorites].
// Ready-made widths for the widescreen: the columns on each side (at 256 pixels a line,
// wideColumnsFor) that make the picture that aspect, and its name.
struct Widescreen {
    int columns;
    const char* name;
};
constexpr Widescreen kWidescreens[] = {{0, "4:3"}, {26, "16:10"}, {43, "16:9"}};
constexpr int kWidescreenCount = 3;

struct Settings : SettingsBase {
    // [library] system: the tabs of the consoles, by ConsoleSystem.
    static constexpr const char* kSystemKeys[kSystemCount] = {"gameboy", "gameboycolor", "nes", "snes"};
    // What [library]'s comment says after the program's name.
    static constexpr const char* kLibraryComment =
        " opens without a game. system: the chosen tab (all, history, favorites,\n"
        "# gameboy, gameboycolor, nes or snes).\n"
        "# view: grid, list or boxes. details: the right-hand panel with the chosen game's data.\n";

    // The music through the synthesizer, as General MIDI instruments on a SoundFont
    // (empty: the included GeneralUser GS), instead of the chip's own sound.
    bool synth = false;
    std::string soundfont;
    int chipMix = 0;  // how loud the chip's own music stays under it, 0-100
    int reverb = 40, chorus = 20;  // 0-100
    bool effectsOriginal = true;  // the sound effects keep the chip's sound under the synthesizer
    Region region = Region::Auto;
    // What is in the second port (the first always has a pad).
    std::array<PadType, 2> padType{PadType::Pad, PadType::Pad};

    // Reads the sections it knows from `path`, ignoring the rest.
    static Settings load(const std::string& path);
    // Applies the sections it knows from `in` onto `s`, leaving the rest of `s` as it is.
    static void parse(std::istream& in, Settings& s);
    // Rewrites the whole file: the controls from `input` plus these settings.
    bool save(const std::string& path, const InputConfig& input) const;
    // Writes every section of these settings (not the controls), with comments.
    void write(std::ostream& out) const;

    // What settings_ini.cpp (common) leaves to this program: the keys of [audio] past the
    // volume, [console] and the sections only it has, and the lines it writes for them.
    void parseKey(const std::string& section, const std::string& key, const std::string& value);
    void writeAudio(std::ostream& out) const;
    void writeConsole(std::ostream& out) const;
    void writeAfterLibrary(std::ostream& out) const;
};

}  // namespace plumnin
