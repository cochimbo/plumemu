#pragma once

// The pieces of the window's main loop (main.cpp) that each program's AppMain (its
// app_main.h) works with too: the files, the folders, the game being played and the loop's
// state it reaches. Built by each program, in its namespace: it includes the program's
// frontend/app_console.h, which says what its console is and how a game goes into it.
#include <SDL.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/app_info.h"
#include "core/i18n.h"
#include "frontend/achievements.h"
#include "frontend/app_console.h"
#include "frontend/crash.h"
#include "frontend/game_options.h"
#include "frontend/input.h"
#include "frontend/settings.h"
#include "frontend/ui.h"
#include "render/gl_renderers.h"

namespace PLUM_APP_NAMESPACE {

constexpr int kSlots = 10;
constexpr size_t kRewindBytes = size_t{256} << 20;  // memory the rewind snapshots may take

inline std::string withoutExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return path;
    return path.substr(0, dot);
}

inline std::string fileName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

inline bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    SDL_RWops* f = SDL_RWFromFile(path.c_str(), "rb");
    if (!f) return false;
    const Sint64 size = SDL_RWsize(f);
    out.resize(size > 0 ? static_cast<size_t>(size) : 0);
    const bool ok = size > 0 && SDL_RWread(f, out.data(), 1, out.size()) == out.size();
    SDL_RWclose(f);
    return ok;
}

inline bool writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    SDL_RWops* f = SDL_RWFromFile(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = SDL_RWwrite(f, data.data(), 1, data.size()) == data.size();
    SDL_RWclose(f);
    return ok;
}

// Portable use (the .ini next to the executable) keeps everything there; otherwise
// the per-user settings folder, since the install folder may not be writable.
struct Paths {
    std::string ini;
    std::string states;     // folder for savestates, ending in a separator
    std::string profiles;   // folder for the games' 3D profiles, ending in a separator
    std::string cheats;     // folder for the games' cheat lists
    std::string covers;     // folder for the library's downloaded covers
    std::string info;       // folder for the library's game data, descriptions and screenshots
    std::string videos;     // folder for the games' video and GIF recordings
    std::string firmware;   // folder for the firmware of cartridges' chips (dsp1b.rom...)
    std::string soundfont;  // the SoundFont that comes with the program, next to the executable
    std::string sound;      // the instrument editor's folder: assignments, recordings, the user's SoundFonts
    std::string models;     // the 3D view's console models that come with the program, next to the executable
    std::string myModels;   // the user's own, which take their place
};

inline Paths findPaths() {
    const char* const kSoundfont = "soundfonts/GeneralUser-GS.sf2";
    auto in = [&](const std::string& dir, const std::string& base) {
        return Paths{dir + appInfo().iniName, dir + "states/",     dir + "profiles/",  dir + "cheats/",
                     dir + "covers/",         dir + "game-info/",  dir + "recordings/", dir + "firmware/",
                     base + kSoundfont,       dir + "sound/",      base + "models/",   dir + "models/"};
    };
    std::string base;
    if (char* b = SDL_GetBasePath()) {
        base = b;
        SDL_free(b);
        const std::string portable = base + appInfo().iniName;
        if (SDL_RWops* f = SDL_RWFromFile(portable.c_str(), "rb")) {
            SDL_RWclose(f);
            return in(base, base);
        }
    }
    if (char* pref = SDL_GetPrefPath(appInfo().dataOrg, appInfo().dataApp)) {
        const std::string dir = pref;
        SDL_free(pref);
        return in(dir, base);
    }
    return in(base, base);
}

// The game being played, and what goes with it: its save, states, cheats and options.
struct App {
    std::unique_ptr<GameConsole> console;  // null while no game is loaded, where the console goes with the game
    bool romLoaded = false;
    std::string romPath;
    std::string statesDir;
    std::string profilesDir;
    std::string cheatsDir;
    std::vector<Cheat> cheats;  // the loaded game's
    std::vector<SlotInfo> slots = std::vector<SlotInfo>(kSlots);
    std::string lastError;  // why the last load failed
    // Joined a netplay game: the console runs the host's game, cartridge save
    // included, which must not overwrite ours. Until another game is loaded.
    bool hostsBattery = false;
    std::string achievementsHash;  // the game as RetroAchievements knows it
    bool noCheats = false;         // RetroAchievements' hardcore mode: the cheats stay off

    // The console's frame rate, or a TV's while there is none.
    double refreshRate() const { return console ? console->refreshRate() : 60.0; }

    std::string savePath() const { return withoutExtension(romPath) + ".sav"; }
    std::string statePath(int slot) const {
        return statesDir + withoutExtension(fileName(romPath)) + ".s" + std::to_string(slot);
    }
    // A game's own options (3D profile included) and its cheats, by the game's path.
    std::string profilePath(const std::string& game) const { return profilesDir + withoutExtension(fileName(game)) + ".ini"; }
    std::string cheatsPath(const std::string& game) const { return cheatsDir + withoutExtension(fileName(game)) + ".txt"; }
    std::string cheatsPath() const { return cheatsPath(romPath); }

    // Hands the console the codes that are on and valid.
    void applyCheats() {
        if (!console) return;
        std::vector<CheatPatch> patches;
        if (!noCheats) patches = decodeCheats(*console, cheats);
        console->setCheats(patches);
    }

    void loadCheats() {
        cheats = readCheats(romPath);
        applyCheats();
    }

    void saveCheats() { writeCheats(romPath, cheats); }

    std::vector<Cheat> readCheats(const std::string& game) const {
        std::vector<uint8_t> data;
        if (!readFile(cheatsPath(game), data)) return {};
        return parseCheats(std::string(data.begin(), data.end()));
    }

    void writeCheats(const std::string& game, const std::vector<Cheat>& list) const {
        std::error_code ec;
        if (list.empty()) {
            std::filesystem::remove(std::filesystem::u8path(cheatsPath(game)), ec);
            return;
        }
        std::filesystem::create_directories(std::filesystem::u8path(cheatsDir), ec);
        const std::string text = formatCheats(list);
        writeFile(cheatsPath(game), std::vector<uint8_t>(text.begin(), text.end()));
    }

    // The options `game` keeps of its own, over `general`.
    GameOverrides readOverrides(const std::string& game, const Settings& general, const InputConfig& generalInput) const {
        std::vector<uint8_t> data;
        if (game.empty() || !readFile(profilePath(game), data)) return {{}, general, generalInput};
        return parseGameOverrides(std::string(data.begin(), data.end()), general, generalInput);
    }

    void writeOverrides(const std::string& game, const GameOverrides& o) const {
        std::error_code ec;
        const std::string text = formatGameOverrides(o, fileName(game));
        if (text.empty()) {
            std::filesystem::remove(std::filesystem::u8path(profilePath(game)), ec);
            return;
        }
        std::filesystem::create_directories(std::filesystem::u8path(profilesDir), ec);
        writeFile(profilePath(game), std::vector<uint8_t>(text.begin(), text.end()));
    }

    // The game's battery-backed memory, next to it.
    void saveBattery() {
        if (!romLoaded || hostsBattery) return;
        PLUM_APP_NAMESPACE::saveBattery(*console, savePath());
    }

    // `test`: a test pattern, looking like that console, instead of a game.
    bool load(const std::string& path, SDL_Window* window, int test = -1) {
        saveBattery();
        std::string error;
        if (!openConsole(console, path, test, &error)) {
            lastError = error;
            // With a window the caller shows it inside the window: a system message box
            // can open behind the game in full screen and never be seen.
            if (!window) std::fprintf(stderr, "%s\n", error.c_str());
            return false;
        }
        romPath = path;
        setCrashContext(path);
        hostsBattery = false;
        lastError.clear();
        achievementsHash = Achievements::gameHash(*console);  // before cheats change the ROM
        loadBattery(*console, savePath());
        romLoaded = true;
        loadCheats();
        if (window) {
            std::string title = appTitle() + " - " + fileName(path);
            const std::string name = consoleTitle(*console);
            if (!name.empty()) title += " (" + name + ")";
            SDL_SetWindowTitle(window, title.c_str());
        }
        refreshSlots();
        return true;
    }

    // Closes the game: its cartridge save is written and nothing runs until another is loaded.
    void unload(SDL_Window* window) {
        if (!romLoaded) return;
        saveBattery();
        romLoaded = false;
        romPath.clear();
        setCrashContext("");
        hostsBattery = false;
        cheats.clear();
        closeConsole(console);
        if (window) SDL_SetWindowTitle(window, appTitle().c_str());
        refreshSlots();
    }

    // Reads every slot's date and thumbnail for the Estado menu.
    void refreshSlots() {
        for (int i = 0; i < kSlots; ++i) refreshSlot(i);
    }

    void refreshSlot(int i) {
        SlotInfo& s = slots[static_cast<size_t>(i)];
        if (s.texture) {
            gl::DeleteTextures(1, &s.texture);
            s.texture = 0;
        }
        s.used = false;
        s.when.clear();
        if (!romLoaded) return;
        std::error_code ec;
        const auto path = std::filesystem::u8path(statePath(i));
        if (!std::filesystem::exists(path, ec)) return;
        s.used = true;
        // The file's modification time, as local date and time.
        const auto written = std::filesystem::last_write_time(path, ec);
        if (!ec) {
            const auto age = std::filesystem::file_time_type::clock::now() - written;
            const std::time_t t = std::time(nullptr) - std::chrono::duration_cast<std::chrono::seconds>(age).count();
            char text[32];
            if (std::strftime(text, sizeof text, "%d/%m/%Y %H:%M", std::localtime(&t))) s.when = text;
        }
        std::vector<uint8_t> data;
        std::vector<uint32_t> pixels;
        if (!readFile(statePath(i), data) || !GameConsole::stateThumbnail(data, pixels, &s.width, &s.height)) return;
        gl::GenTextures(1, &s.texture);
        gl::BindTexture(gl::TEXTURE_2D, s.texture);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, s.width, s.height, 0, gl::BGRA, gl::UNSIGNED_INT_8_8_8_8_REV,
                       pixels.data());
    }

    std::string saveState(int slot) {
        std::vector<uint8_t> data;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(statesDir), ec);
        if (!console->saveState(data) || !writeFile(statePath(slot), data))
            return trf("No se pudo guardar el estado en %s: ¿falta espacio en el disco?", statePath(slot).c_str());
        refreshSlot(slot);
        return trf("Estado guardado en la ranura %d", slot);
    }

    std::string loadState(int slot, bool* loaded = nullptr) {
        if (loaded) *loaded = false;
        std::vector<uint8_t> data;
        if (!readFile(statePath(slot), data)) return trf("La ranura %d está vacía", slot);
        std::string error;
        if (!console->loadState(data, &error))
            return trf("No se pudo cargar el estado: %s. La partida sigue como estaba.", error.c_str());
        if (loaded) *loaded = true;
        return trf("Estado cargado de la ranura %d", slot);
    }
    // The achievements' progress when the state in `slot` was saved, next to it.
    std::string progressPath(int slot) const { return statePath(slot) + ".achievements"; }
};

// What of the main loop each program's AppMain reaches.
struct MainContext {
    const Paths& paths;
    Settings& settings;
    Ui& ui;
    Input& input;
    App& app;
    SDL_Window* window;
    bool capturing;
    std::function<bool()> hardcore;    // RetroAchievements' hardcore mode is on
    std::function<bool()> netPlaying;  // a netplay game is connected (or connecting)
    std::function<void()> applyConsole;
};

}  // namespace PLUM_APP_NAMESPACE
