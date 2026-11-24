#pragma once

#include <array>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "../core/cartridge.h"
#include "../core/io.h"
#include "../core/system.h"
#include "render/renderer.h"
#include "frontend/input.h"
#include "frontend/settings_base.h"

namespace plumsg {

// Everything the menus change apart from the controls. Lives in plumsg.ini next to
// the [teclado] / [mando] sections, under [interfaz], [video], [audio], [consola],
// [velocidad], [estados], [biblioteca], [bios], [red], [logros], [screenscraper], [recientes],
// [history] and [favorites].
// Ready-made widths for the widescreen: the columns on each side (at 320 pixels a line,
// VdpMd::wideColumnsFor) that make the picture that aspect, and its name.
struct Widescreen {
    int columns;
    const char* name;
};
constexpr Widescreen kWidescreens[] = {{0, "4:3"}, {32, "16:10"}, {53, "16:9"}};
constexpr int kWidescreenCount = 3;

struct Settings : SettingsBase {
    // [library] system: the tabs of the consoles, by ConsoleSystem.
    static constexpr const char* kSystemKeys[kSystemCount] = {"megadrive", "32x", "megacd", "mastersystem", "gamegear"};
    // What [library]'s comment says after the program's name.
    static constexpr const char* kLibraryComment =
        " opens without a game. system: the chosen tab (all, history, favorites, megadrive, 32x,\n"
        "# megacd, mastersystem or gamegear). view: grid, list or boxes. details: the right-hand panel with the chosen game's data.\n";

    // The Roland sound: the FM and PSG music played as General MIDI on a SoundFont.
    bool roland = false;
    std::string soundfont;  // empty: the one that comes with plumsg
    int chipMix = 0;        // how much of the chips' own music stays, 0-100
    int reverb = 40;        // 0-100
    int chorus = 20;        // 0-100
    std::array<int, 5> equalizer{};  // the synthesizer's, dB per band (RolandSound::kEqBands), -12..12
    bool effectsOriginal = true;  // the game's sound effects keep the chips' sound
    bool compareSound = true;     // the rules' instruments tuned by comparing sounds
    Region region = Region::Auto;
    std::array<PadType, 2> padType{PadType::SixButton, PadType::SixButton};
    bool smsFm = false;  // the Master System's FM sound unit on every console, not only Japanese ones
    // The Mega CD's BIOS for each region (Japan, USA, Europe); plumsg does not include them.
    std::array<std::string, 3> cdBios;

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

}  // namespace plumsg
