#pragma once

// What plumnin's window does that plumsg's does not, called by the common main loop
// (common/src/frontend/main.cpp) at its moments: the synthesizer, the songs the game asks
// for, the cartridges' firmware and the test pattern.
#include <memory>
#include <string>

#include "../core/songs.h"
#include "../core/synth_music.h"
#include "core/rewind.h"
#include "frontend/audio.h"
#include "frontend/main_parts.h"

namespace plumnin {

class AppMain {
public:
    // Before anything else: nothing to rename from older versions (prepareFolders), and
    // the firmware folder (settingsLoaded).
    static void prepareFolders(const Paths&, const std::string&) {}
    static void settingsLoaded(const Paths& paths);

    // Once the window, the interface and the console exist; started() after the rest is
    // set up, before any game is loaded.
    void setup(MainContext& m);
    void started() {}

    void applyConsole() {}
    void gameOptionsUsed(const std::string&) {}
    // A game being opened: before, once it is in (loaded), what to tell about it
    // (loadNotices), and when it could not be opened.
    void beforeLoad(const std::string&) {}
    void loaded(const std::string&) { m_->applyConsole(); }
    void loadNotices();
    void loadFailed(const std::string& path);
    // The game given on the command line, loaded (and its state, if one was asked for).
    void commandLineLoaded() { allNotesOff(); }
    // The windows the command line asks for, at the start: `stage` 0 before the
    // achievements', 1 after them and 2 before the cheats'.
    void openWindows(const AppCommandLine& cmd, int stage);
    // A savestate loaded (or not), by the player or by rewinding.
    void stateLoaded(bool loaded);
    void rewound() { allNotesOff(); }

    // The Trucos window opened for `path`.
    void cheatsFor(const std::string& path);
    // Netplay started (the guest plays on pad 2, which has to be plugged in) and ended.
    void netStarted();
    void netStopped() { m_->applyConsole(); }
    void stopping() {}
    void stopped() {}

    // Each turn of the loop: the picture's options, the processor's speed when `overclock`
    // can change, and the synthesizer.
    void everyTurn(bool canOverclock, int overclock);
    bool mouseAims() const { return false; }
    // The interface's state, and what it asked for.
    void fillUiState(UiState& state, bool paused, bool netSession);
    void actions(const UiActions&, bool&, bool) {}
    // Plays the instrument editor's song instead of the game, when it plays one: true then.
    bool playMusic(double frameSeconds, Audio& audio, FramePacer& pacer, double& turnTarget);
    double musicSpeed() const { return 1.0; }
    void aim(bool) {}

private:
    void allNotesOff();
    void watchSongs(std::vector<SynthMusic::Song> songs, bool keepAsked);
    void applySynth();

    MainContext* m_ = nullptr;
    // The synthesizer, when chosen and its SoundFont opens; else the chip's own sound.
    std::unique_ptr<SynthMusic> synth_ = std::make_unique<SynthMusic>();
    std::string soundDir_;
    std::string synthFont_;  // the SoundFont loaded (or tried)
    bool synthFontOk_ = false;
    const Console* synthConsole_ = nullptr;
    std::string synthGame_;
    // The songs the game asks for, as Sacar canciones found how: the synthesizer knows by
    // them which one is playing.
    SongWatcher songWatcher_;
    ConsoleSystem cheatsSystem_ = ConsoleSystem::Nes;  // the cheats' game's console
};

}  // namespace plumnin
