#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cheats.h"
#include "core/console_base.h"
#include "pad.h"
#include "core/state_io.h"
#include "synth_music.h"
#include "vgm.h"
#include "system.h"
#include "core/video_frame.h"

namespace plumnin {

// The sound a console made, at the rate the frontend asked for: interleaved stereo.
class AudioOut {
public:
    const std::vector<int16_t>& samples() const { return out_; }
    void clearSamples() { out_.clear(); }
    std::vector<int16_t>& output() { return out_; }

private:
    std::vector<int16_t> out_;
};

// A piece of the console's memory as RetroAchievements and netplay see it: `size`
// bytes at `address` of the console's own map (RetroAchievements' for that console).
struct MemoryRegion {
    uint32_t address = 0;
    uint8_t* data = nullptr;
    size_t size = 0;
};

// One of plumnin's consoles, as the frontend, the headless runner, netplay and the
// achievements drive it. Each console (Game Boy, NES, Super Nintendo) is a subclass;
// what they all do the same way is done here (reading the file, pads, the sound's
// extras) and in plum::ConsoleBase (frames, the savestate's container, run-ahead).
class Console : public plum::ConsoleBase {
public:
    virtual ConsoleSystem system() const = 0;

    // A cartridge from a file (.zip too), or already in memory. False, with the reason
    // in Spanish, if this console cannot run it.
    bool loadRom(const std::string& path, std::string* error);
    bool loadRomData(std::vector<uint8_t> rom, std::string* error = nullptr);
    // Where to look for the firmware some cartridges' chips need (the DSP-1's...), after
    // the game's own folder and a "firmware" folder inside it. For every console.
    static void setFirmwareFolders(std::vector<std::string> folders);
    bool loaded() const { return !rom_.empty(); }
    // The cartridge as it was loaded, before any cheat, and its CRC-32.
    const std::vector<uint8_t>& rom() const { return rom_; }
    uint32_t romCrc() const { return romCrc_; }
    // The game's name as its header gives it, empty if it gives none.
    virtual std::string title() const { return {}; }
    // Something worth telling about the game just loaded (empty: nothing).
    const std::string& loadNotice() const { return loadNotice_; }

    void setRegion(Region r);
    Region region() const { return region_; }
    // The region the game runs in now: the one chosen, or the cartridge's own on Auto.
    virtual Region effectiveRegion() const { return Region::UsaNtsc; }

    // How many controller ports it has (the Game Boy one, its own buttons).
    virtual int ports() const { return 2; }
    void setPad(int port, const PadState& s) {
        if (port >= 0 && port < 2) pads_[static_cast<size_t>(port)] = s;
    }
    void setPads(const PadState& one, const PadState& two) { pads_ = {one, two}; }
    void setPadType(int port, PadType t) {
        if (port >= 0 && port < 2) padTypes_[static_cast<size_t>(port)] = t;
    }
    PadType padType(int port) const { return padTypes_[static_cast<size_t>(port & 1)]; }

    // refreshRate(): 59.73 on the Game Boy, 60.1 on the NES...

    // Options of the picture, which only change what is drawn.
    virtual void setUnlimitedSprites(bool) {}
    virtual void setWidescreen(int /*left*/, int /*right*/) {}
    // The console's processor faster than the real one, in percent (100 is real).
    static constexpr int kMinOverclock = 100, kMaxOverclock = 400;
    virtual void setOverclock(int) {}

    // The sound made by the last frames, at setSampleRate()'s rate.
    AudioOut& audio() { return audio_; }
    const AudioOut& audio() const { return audio_; }

    // The synthesizer (not owned; null for the chip's own sound): it plays the music as
    // General MIDI notes on a SoundFont, added to this console's sound. `chipPercent` is
    // how loud the chip's own sound of what it plays stays (0-100). A console that cannot
    // be heard through it (synthSupported() false) ignores it.
    void setSynth(SynthMusic* synth, int chipPercent);
    SynthMusic* synth() const { return synth_; }
    virtual bool synthSupported() const { return false; }
    // Records the music: from now on every write to the sound chip goes to `recorder`
    // (not owned; null stops, marking the end at now), which begins with the chip as it
    // is. Only for the
    // consoles whose chip a VGM can hold (musicRecordable()); false for the rest.
    bool recordMusic(VgmWriter* recorder);
    VgmWriter* musicRecorder() const { return recorder_; }
    virtual bool musicRecordable() const { return false; }
    // The music as it is now, as a file that goes on playing it (the SNES's, an SPC: its
    // sound processor keeps the song and plays it by itself); empty for the consoles
    // that have none.
    virtual std::vector<uint8_t> musicSnapshot(const std::string& /*title*/, int /*seconds*/) { return {}; }

    // The cartridge's battery-backed memory, if it has any.
    virtual bool hasSaveData() const { return false; }
    virtual std::vector<uint8_t> saveData() const { return {}; }
    virtual void loadSaveData(const std::vector<uint8_t>&) {}
    // Written to since the last clearSaveDataChanged().
    virtual bool saveDataChanged() const { return false; }
    virtual void clearSaveDataChanged() {}

    // The console's cheat codes: how to read them, and which writes are on.
    virtual CheatDecoder cheatDecoder() const { return {}; }
    virtual void setCheats(const std::vector<CheatPatch>&) {}

    // The memory RetroAchievements reads, in the order of its address map.
    virtual std::vector<MemoryRegion> memoryMap() { return {}; }
    // RetroAchievements' number for the console (RC_CONSOLE_*), 0 if it has none.
    virtual uint32_t achievementsConsole() const { return 0; }
    // What both sides of a netplay game compare to see they still run the same game.
    virtual uint32_t checksum();

    // Savestates (saveState, loadState: plum::ConsoleBase). The picture saved inside a
    // state file, 0xFFRRGGBB.
    static bool stateThumbnail(const std::vector<uint8_t>& state, std::vector<uint32_t>& pixels, int* width,
                               int* height);

protected:
    // plumnin's savestates: "PLNS", with the console and the region after the CRC, and
    // the pads' types in a block of their own.
    static const StateFormat kStateFormat;
    const StateFormat& stateFormat() const override { return kStateFormat; }
    std::string stateError(StateError e) const override;
    uint32_t stateCrc() const override { return romCrc_; }
    void saveStateHeader(StateWriter& w) override;
    std::string checkStateHeader(const uint8_t* header) const override;
    void saveAppBlocks(StateBlocks& blocks) override;
    bool loadAppBlock(uint32_t id, StateReader& r) override;
    void beginRunAhead() override;
    void endRunAhead() override;

    // Called by loadRom: the console takes the cartridge, or says why it cannot.
    virtual bool insert(const std::vector<uint8_t>& rom, std::string* error) = 0;
    // Called by setRegion when the region changes.
    virtual void regionChanged() {}

    // The firmware file `name` (inside insert), empty if no folder has it.
    std::vector<uint8_t> findFirmware(const std::string& name) const;

    std::array<PadState, 2> pads_{};
    std::array<PadType, 2> padTypes_{PadType::Pad, PadType::Pad};
    // The chip's channels heard at full volume (a bit per synthesizer voice); the rest at
    // `percent`.
    // The chip's channels heard at full volume (a bit per synthesizer voice), those not
    // heard at all, and the rest at `percent`.
    virtual void setChipVoices(uint32_t /*full*/, uint32_t /*silent*/, int /*percent*/) {}
    // Tells the chip which channels the synthesizer leaves to it, when that changes.
    void updateChipVoices();

    AudioOut audio_;
    SynthMusic* synth_ = nullptr;
    VgmWriter* recorder_ = nullptr;
    // Starts `recorder` with the chip's registers as they are.
    virtual void beginMusicRecording(VgmWriter& recorder) { (void)recorder; }
    // Brings `recorder` up to now, the waits included.
    virtual void endMusicRecording(VgmWriter& recorder) { (void)recorder; }
    int synthPercent_ = 100;
    uint32_t chipFull_ = 0, chipSilent_ = 0;
    bool chipSet_ = false;
    double outputRate_ = 44100;  // what setSampleRate() last asked for
    std::string loadNotice_;

private:
    bool loadRomFrom(std::vector<uint8_t> rom, const std::string& path, std::string* error);

    std::vector<uint8_t> rom_;
    std::string romPath_;  // the file of the game being loaded, empty if from memory
    uint32_t romCrc_ = 0;
    Region region_ = Region::Auto;
    // What run-ahead took away from its frames.
    SynthMusic* aheadSynth_ = nullptr;
    VgmWriter* aheadRecorder_ = nullptr;
};

// Whether plumnin can run that console's games yet.
bool emulated(ConsoleSystem s);
// A console of that kind, or null if plumnin does not emulate it yet.
std::unique_ptr<Console> createConsole(ConsoleSystem s);
// The console for that file, with the game loaded; null with the reason in `error`.
std::unique_ptr<Console> openGame(const std::string& path, std::string* error);
// How that console's cheat codes are read (empty: it has none yet), for the Trucos
// window even when no game of it is running, and their kinds, to show ("Game Genie (...)").
CheatDecoder cheatDecoderFor(ConsoleSystem s);
const char* cheatFormatsFor(ConsoleSystem s);

}  // namespace plumnin
