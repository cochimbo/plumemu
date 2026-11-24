#pragma once

// What both programs' headless runners do the same way once their console is set up:
// the options they share, the input script, the frames (alone or in a netplay game),
// the logs and checks, the files asked for and the summary. Each program parses its own
// options around these, builds its console and says, through a RunGame, what only it
// knows (its sound's extras, its memory, its names).
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "core/console_base.h"
#include "core/video_frame.h"
#include "headless/headless_tools.h"

namespace plum {
namespace headless {

// The command line, one argument at a time, with the values options take.
class Args {
public:
    Args(int argc, char** argv) : argc_(argc), argv_(argv) {}
    // Moves to the next argument; false when there are no more.
    bool next() {
        missing_ = false;
        return ++i_ < argc_ && (arg_ = argv_[i_], true);
    }
    const std::string& arg() const { return arg_; }
    // The option's value: the argument after it.
    std::string value();
    // A number of 0 or more, or a hash in hex, as the option's value.
    void number(long* out);
    void hash(uint32_t* out);
    // An option without its value, or with a value that is not valid.
    bool missing() const { return missing_; }
    void setMissing() { missing_ = true; }

private:
    int argc_;
    char** argv_;
    int i_ = 0;
    std::string arg_;
    bool missing_ = false;
};

// The options both runners take, with what they need to run.
struct RunOptions {
    std::string game;
    long frames = 600, overclock = 100, pngEvery = 0;
    bool noPad2 = false, noSpriteLimit = false, quiet = false;
    std::string stateIn, stateOut, ramOut, hashLog, json, expectLog;
    std::string png, layers, wav, gifOut, aviOut, vgmOut, mp3Out;
    std::string outDir = ".";
    std::vector<std::string> cheats, scripts, lines;
    bool haveExpectHash = false, haveExpectAudio = false;
    uint32_t expectHash = 0, expectAudio = 0;
    long netPort = 0, inputDelay = 2, netLag = 0, netLoss = 0;
    bool netHost = false;
    std::string netJoin;

    // Takes args.arg() if it is one of these options (with its value); false if not.
    bool parse(Args& args);
    bool netplay() const { return netHost || !netJoin.empty(); }

    // What loadInput() made of --input, --do and --expect-log.
    std::vector<Command> commands;
    std::vector<LogLine> expected;
    // Reads the input script and the expected log, and checks netplay's options: -1 if
    // all is well, else the exit code (after saying why).
    int loadInput();
};

// A savestate file into the console, saying why not when it cannot.
bool loadStateFile(ConsoleBase& console, const std::string& path);

// What only each program knows about the game being run.
class RunGame {
public:
    virtual ~RunGame() = default;
    virtual ConsoleBase& console() = 0;
    // The sound of the frames run since the last clearAudio().
    virtual const std::vector<int16_t>& samples() = 0;
    // Before each frame (the same sound on every run), and after a script's loadstate.
    virtual void beforeFrame() {}
    virtual void stateLoaded() {}
    // The console's work RAM as --dump-ram writes it.
    virtual bool writeRam(const std::string& path) = 0;
    // How wide --gif and --avi are, from their first frame.
    virtual int recordingWidth(const VideoFrame& first) { return first.width; }
    // The program's own files, after the pictures (false if one could not be written).
    virtual bool writeOutputs() { return true; }
    // The summary: the game's name, its console, whether it ran at 50 Hz, and what the
    // program adds after the sound's hash (as text and inside the JSON).
    virtual std::string title() = 0;
    virtual std::string system() = 0;
    virtual bool pal() = 0;
    virtual void printDetails() {}
    virtual void jsonDetails(std::ostream&) {}
};

// Runs the game for options.frames frames and writes what was asked for: the exit code
// (0 all well, 1 a file or the network failed, 3 a check failed).
int run(RunGame& game, const RunOptions& options);

}  // namespace headless
}  // namespace plum
