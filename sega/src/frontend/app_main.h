#pragma once

// What plumsg's window does that plumnin's does not, called by the common main loop
// (common/src/frontend/main.cpp) at its moments: the Roland sound, the instrument editor,
// its recordings and the Partitura, the Mega CD's BIOS and the light guns.
#include <memory>
#include <set>
#include <string>

#include "../core/roland.h"
#include "../core/vgm.h"
#include "core/rewind.h"
#include "frontend/audio.h"
#include "frontend/main_parts.h"
#include "instrument_editor.h"
#include "light_gun.h"
#include "score_view.h"

namespace plumsg {

class AppMain {
public:
    // Before anything else: the folders and files of old versions get their new names
    // (prepareFolders), and what the settings, just read, need on disk (settingsLoaded).
    static void prepareFolders(const Paths& paths, const std::string& settingsDir);
    static void settingsLoaded(const Paths&) {}

    // Once the window, the interface and the console exist; started() after the rest is
    // set up, before any game is loaded.
    void setup(MainContext& m);
    void started() { applySound(); }

    // What the program sets in the console besides its region and pads (applyConsole).
    void applyConsole();
    // A game's own options taken (or the general ones, for `path` empty).
    void gameOptionsUsed(const std::string& path);
    // A game being opened: before, once it is in (loaded), what to tell about it
    // (loadNotices), and when it could not be opened.
    void beforeLoad(const std::string& path);
    void loaded(const std::string& path);
    void loadNotices();
    void loadFailed(const std::string& path);
    // The game given on the command line, loaded (and its state, if one was asked for).
    void commandLineLoaded() { roland_->allNotesOff(); }
    // The windows the command line asks for, at the start: `stage` 0 before the
    // achievements', 1 after them and 2 before the cheats'.
    void openWindows(const AppCommandLine& cmd, int stage);
    // A savestate loaded (or not), by the player or by rewinding.
    void stateLoaded(bool) { roland_->allNotesOff(); }
    void rewound() { roland_->allNotesOff(); }

    // The Trucos window opened for `path`.
    void cheatsFor(const std::string& path);
    // Netplay started (the guest plays on pad 2, which has to be plugged in) and ended.
    void netStarted();
    void netStopped();
    // The game is being stopped, and has been (also when the program quits: stopping()).
    void stopping() { stopRecording(); }
    void stopped() { roland_->allNotesOff(); }

    // Each turn of the loop: the picture's options, and the processor's speed when
    // `overclock` can change.
    void everyTurn(bool canOverclock, int overclock);
    // The mouse aims a light gun (and does not turn the 3D camera).
    bool mouseAims() const;
    // The interface's state, and what it asked for.
    void fillUiState(UiState& state, bool paused, bool netSession);
    void actions(const UiActions& act, bool& paused, bool padMenuOpen);
    // Plays the instrument editor's track instead of the game, when it plays one: true
    // then.
    bool playMusic(double frameSeconds, Audio& audio, FramePacer& pacer, double& turnTarget);
    // How fast the game runs for the Partitura (1 normal).
    double musicSpeed() const { return musicSpeed_; }
    // The light guns' and paddles' state, after the pads', for the frame about to run.
    void aim(bool keyboard);

private:
    bool loadSoundfont();
    void applyInstruments(const std::string& path);
    void applySound();
    void stopRecording();
    bool smsAimed(int port) const;

    MainContext* m_ = nullptr;
    std::unique_ptr<RolandSound> roland_ = std::make_unique<RolandSound>();  // before the console, which points at it
    std::unique_ptr<InstrumentEditor> editor_;
    // The Roland sound, when chosen and its SoundFont opens; else the chips' own.
    std::string rolandFont_;  // the SoundFont loaded (or tried)
    bool rolandFontOk_ = false;
    // The game's instruments switched off in the Partitura, until another game loads.
    std::set<uint32_t> gameSilenced_;
    // The instrument editor records the game's music while it is played.
    VgmWriter recorder_;
    bool recording_ = false;
    double musicSpeed_ = 1.0;  // the Partitura's speed
    bool cheatsSms_ = false;   // the cheats' game is a Master System or Game Gear one
    // The Partitura: the editor's track while it has one open, else the game's notes.
    ScoreMusic scoreMusic_;
    bool trackScore_ = false;
    // The light guns' crosshairs, while the second port has one and a Mega Drive game runs,
    // or a Master System game has a Light Phaser or a paddle (whose knob follows the
    // crosshair across) in a port.
    LightGunAim gunAim_;
    std::string gunRom_;  // the game gunAim's second gun was last used in
    int lastMouseX_ = -1, lastMouseY_ = -1;
    Uint64 lastAim_ = 0;
};

}  // namespace plumsg
