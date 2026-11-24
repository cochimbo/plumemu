#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/pad_state.h"
#include "core/state_io.h"
#include "core/video_frame.h"

namespace plum {

// One emulated console, as both programs' frontends, headless runners, netplay and
// rewind drive it: the Mega Drive (plumsg) and the Game Boy, NES and Super Nintendo
// (plumnin) are subclasses. What they all do the same way is done here: running
// frames with or without drawing them, the savestate's container (header, tagged
// blocks, thumbnail, compression) and run-ahead. What each program adds (its pads,
// regions, sound options) is in its own subclass in between.
class ConsoleBase {
public:
    virtual ~ConsoleBase() = default;
    ConsoleBase() = default;
    ConsoleBase(const ConsoleBase&) = delete;
    ConsoleBase& operator=(const ConsoleBase&) = delete;

    virtual void reset() = 0;       // the console's reset button: RAM is kept
    virtual void powerCycle() = 0;  // switched off and on

    // Runs up to the start of the next frame, and draws it unless told not to.
    virtual void runFrame() = 0;
    virtual const VideoFrame& frame() const = 0;
    void setDrawing(bool on) { drawing_ = on; }
    bool drawing() const { return drawing_; }
    // Frames run since the game was loaded.
    uint64_t frames() const { return frames_; }
    // Frames per second of the console's picture.
    virtual double refreshRate() const = 0;
    // The sound of the frames to come, at `hz` samples a second.
    virtual void setSampleRate(double hz) = 0;
    // Forgets the sound made and not yet taken (netplay, which runs some frames again).
    virtual void clearAudio() = 0;

    // The two pads, as they are from now on.
    virtual void setPads(const PadState& one, const PadState& two) = 0;
    // The CRC-32 of the game being run, which savestates and netplay are tied to.
    uint32_t gameCrc() const { return stateCrc(); }
    // What both sides of a netplay game compare to see they still run the same game: a
    // hash of the memory where games keep what matters.
    virtual uint32_t checksum() = 0;

    // Savestates: only between frames. A file is a 12-byte header (magic, version, size
    // of the body) and the body deflated; the body is the game's CRC-32, a few bytes of
    // the console's own (its region...), the frame count and then blocks with a
    // four-letter tag, which a reader that does not know one skips. loadState() leaves
    // the console as it was when it fails. A quick state (rewind, run-ahead, netplay)
    // carries no thumbnail and is compressed less.
    bool saveState(std::vector<uint8_t>& out, bool quick = false);
    bool loadState(const std::vector<uint8_t>& in, std::string* error);
    // Runs `frames` frames with the same pads, without sound or recordings, shows the
    // last one's picture (drawn even under setDrawing(false)) and goes back to where it
    // was: run-ahead. False if it could not go back.
    bool runAhead(int frames);

    // How a program's savestate files are made.
    struct StateFormat {
        uint32_t magic = 0;
        uint32_t version = 1;
        uint32_t maxSize = 0;  // largest body it accepts
        int headerBytes = 0;   // the console's own bytes between the CRC and the frame count
    };
    // The picture saved inside a state file of that format, 0xFFRRGGBB.
    static bool stateThumbnail(const StateFormat& format, const std::vector<uint8_t>& state,
                               std::vector<uint32_t>& pixels, int* width, int* height);

protected:
    // Each console's own state, in blocks with a four-letter tag.
    class StateBlocks {
    public:
        explicit StateBlocks(StateWriter& body) : body_(body) {}
        template <class F>
        void add(const char (&tag)[5], F&& describe) {
            StateWriter w;
            describe(w);
            raw(tag, w.data());
        }
        void raw(const char (&tag)[5], const std::vector<uint8_t>& data);

    private:
        StateWriter& body_;
    };
    static constexpr uint32_t fourcc(const char (&tag)[5]) {
        return static_cast<uint32_t>(tag[0]) | static_cast<uint32_t>(tag[1]) << 8 |
               static_cast<uint32_t>(tag[2]) << 16 | static_cast<uint32_t>(tag[3]) << 24;
    }

    // What can go wrong loading a state, for stateError() to say in the program's words.
    enum class StateError { NotAState, Newer, Damaged, BrokenBlock, OtherGame };
    virtual const StateFormat& stateFormat() const = 0;
    virtual std::string stateError(StateError e) const = 0;
    // The CRC-32 a state is tied to: the game's.
    virtual uint32_t stateCrc() const = 0;
    // The console's own header bytes (stateFormat().headerBytes of them).
    virtual void saveStateHeader(StateWriter& w) = 0;
    // Why a state with that header cannot be loaded here (empty: it can).
    virtual std::string checkStateHeader(const uint8_t* /*header*/) const { return {}; }
    // Called when a state that passed the checks starts loading, after the console kept
    // a copy of itself to go back to: what blocks an older state lacks start as they were
    // before those blocks existed.
    virtual void beforeLoadBlocks(const uint8_t* /*header*/) {}
    // The program's blocks, before the console's (saveAppBlocks), and taking one of them
    // when a state loads (true if it was the program's).
    virtual void saveAppBlocks(StateBlocks&) {}
    virtual bool loadAppBlock(uint32_t /*id*/, StateReader& /*r*/) { return false; }
    virtual void saveBlocks(StateBlocks& blocks) = 0;
    // One block of a state being loaded; false if it is broken. Unknown tags are skipped.
    virtual bool loadBlock(uint32_t id, StateReader& r) = 0;
    // Called after a state loaded: what is worked out from the rest is worked out again.
    virtual void afterLoad() {}

    // Run-ahead's frames are not heard: these take the sound's extras (the synthesizer,
    // the recordings) away for them, and give them back.
    virtual void beginRunAhead() {}
    virtual void endRunAhead() {}

    // The picture on screen no longer is the one a state was loaded with (a new game,
    // a reset): the next state draws its own thumbnail.
    void forgetLoadedThumbnail() { loadedThumbnail_.clear(); }

    uint64_t frames_ = 0;
    bool drawing_ = true;

private:
    std::vector<uint8_t> loadedThumbnail_;  // kept until a frame runs after a load
    uint64_t thumbnailFrame_ = 0;
    std::vector<uint8_t> aheadState_;  // runAhead()'s way back, kept to reuse its memory
};

}  // namespace plum
