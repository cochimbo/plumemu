#pragma once

// What both programs' headless runners share: hashes, files, the input script and a
// laggy network for netplay tests. Built by each program (it uses padButtons()).
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "core/netplay.h"
#include "core/pad_state.h"
#include "core/video_frame.h"

namespace plum {
namespace headless {

// The same hash the tests and rom_runner print: FNV-1a over the visible pixels.
uint32_t frameHash(const VideoFrame& f);
uint32_t soundHash(uint32_t h, const std::vector<int16_t>& samples);
bool writeFile(const std::string& path, const void* data, size_t size);
bool readFile(const std::string& path, std::vector<uint8_t>& data);
bool writePng(const std::string& path, const uint32_t* argb, int width, int height, int stride);
bool writeWav(const std::string& path, const std::vector<int16_t>& samples, int rate);
std::string hex8(uint32_t v);
std::string jsonString(const std::string& s);

// What a script line does on its frames.
struct Command {
    enum Kind { Buttons, Screenshot, SaveState, LoadState, DumpRam, Hash, Reset, Power, Quit } kind = Buttons;
    long first = 0, last = 0;  // frames, both included
    PadState pad[2];
    std::string file;
    int line = 0;
};

// "N", "N-M" or "N+K": the frames a script line is for.
bool parseFrames(const std::string& s, long* first, long* last);
// Parses one script line into `out` (nothing for blank and comment lines).
bool parseLine(const std::string& raw, int lineNo, const std::string& where, std::vector<Command>& out);
// Presses on `d` what is pressed on `s`.
void orPad(PadState& d, const PadState& s);

// A slower, lossier network than the real one, in front of the socket: what this side
// sends waits `lagMs` and some packets never leave.
class LaggyTransport : public NetTransport {
public:
    LaggyTransport(NetTransport& inner, long lagMs, long lossPercent)
        : inner_(inner), lag_(static_cast<uint32_t>(lagMs)), loss_(static_cast<uint32_t>(lossPercent)) {}
    void setNow(uint32_t now) {
        now_ = now;
        while (!queue_.empty() && queue_.front().first <= now_) {
            inner_.send(queue_.front().second.data(), queue_.front().second.size());
            queue_.pop_front();
        }
    }
    void send(const uint8_t* data, size_t size) override {
        seed_ = seed_ * 1103515245u + 12345u;
        if ((seed_ >> 16) % 100 < loss_) return;
        queue_.emplace_back(now_ + lag_, std::vector<uint8_t>(data, data + size));
        setNow(now_);
    }
    bool receive(std::vector<uint8_t>& packet) override { return inner_.receive(packet); }
    void keepPeer() override { inner_.keepPeer(); }

private:
    NetTransport& inner_;
    uint32_t lag_, loss_;
    uint32_t now_ = 0;
    uint32_t seed_ = 1;
    std::deque<std::pair<uint32_t, std::vector<uint8_t>>> queue_;
};

struct LogLine {
    long frame;
    uint32_t picture, sound;
};

bool readLog(const std::string& path, std::vector<LogLine>& log);

}  // namespace headless
}  // namespace plum
