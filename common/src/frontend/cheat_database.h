#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/cheat_list.h"
#include "plum.h"

namespace plum {

// The ready-made cheat lists of libretro-database (the cht folder of each console, the
// lists RetroArch offers), downloaded when the Trucos window asks for a game's: the
// lists named like the ROM, and others with a similar name to pick from by hand.
// A worker thread does the downloading; update() hands its results to the main thread.
class CheatDatabase {
public:
    struct View {
        bool busy = false;               // listing or downloading
        std::string status;              // an error, or empty
        std::string game;                // the ROM the lists are for
        std::vector<std::string> files;  // its lists (names without ".cht"), best first
        std::string file;                // the one in `cheats`
        std::vector<Cheat> cheats;       // all off
    };

    CheatDatabase();
    ~CheatDatabase();
    CheatDatabase(const CheatDatabase&) = delete;
    CheatDatabase& operator=(const CheatDatabase&) = delete;

    // Looks for the lists of the game at `path`, a game of console `system` (the
    // program's number for it, see core/systems.h), and downloads the best one.
    void search(const std::string& path, int system);
    // Downloads another of the lists found.
    void load(const std::string& file);
    // Main thread, once a frame while the window is open.
    void update();
    const View& view() const { return view_; }
    void clear();

private:
    struct Task {
        bool search = false;  // list and load the best; else load `file`
        unsigned serial = 0;
        int system = 0;
        std::string path, file;
    };
    struct Reply {
        unsigned serial = 0;
        bool search = false;
        std::string status;
        std::vector<std::string> files;
        std::string file;
        std::vector<Cheat> cheats;
    };

    void ask(Task task);
    void work();
    bool loadIndex(int s, std::string* error);
    bool download(int s, const std::string& file, std::vector<Cheat>* out, int* status);

    View view_;  // main thread only
    int system_ = 0;
    unsigned serial_ = 0;

    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false, hasTask_ = false, hasReply_ = false;
    Task task_;
    Reply reply_;
    std::vector<std::vector<std::string>> index_;  // worker only, one per console
    std::thread worker_;
};

}  // namespace plum
