#pragma once

// The VGM file format that both programs' recordings and players share: the header's
// layout, the waits between writes, the GD3 tag, and gzip for .vgz. What each sound chip's
// commands mean, and the chips themselves, are each program's (core/vgm.h).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "plum.h"

namespace plum {
namespace vgm {

constexpr double kTickRate = 44100.0;  // VGM's time unit: samples at 44.1 kHz

// Little-endian 32-bit fields: read (0 past the end), written in place, appended.
uint32_t get32(const std::vector<uint8_t>& d, size_t at);
void put32(std::vector<uint8_t>& d, size_t at, uint32_t v);
void append32(std::vector<uint8_t>& d, uint32_t v);

// GD3 tags are UTF-16; the programs' strings are UTF-8. Appends `s` with its terminator.
void appendUtf16(std::vector<uint8_t>& d, const std::string& s);
// Reads one UTF-16 string at `at`, moving past its terminator.
std::string readUtf16(const std::vector<uint8_t>& d, size_t& at, size_t end);

// A .vgz is a .vgm in gzip. gzip() returns `in` as it is if it cannot compress it;
// gunzip() is false if `in` is not gzip.
std::vector<uint8_t> gzip(const std::vector<uint8_t>& in);
bool gunzip(const std::vector<uint8_t>& in, std::vector<uint8_t>* out);

// Appends the wait commands that take a recording from `*samples` to `target` (44.1 kHz
// ticks), and moves `*samples` there. With `frameWaits`, a wait of exactly one 60 Hz or
// 50 Hz frame is written as its one-byte command.
void appendWait(std::vector<uint8_t>& data, uint64_t* samples, uint64_t target, bool frameWaits);

// Ends a file whose header and commands are in `file` (the end command already there):
// sets where its GD3 tag is and appends it (`fields`: title, game and system, each in
// English and Japanese, author, date, who made the file and notes), then the file's size.
void appendTag(std::vector<uint8_t>& file, const std::vector<std::string>& fields);

// What every VGM's header says, read from `d` (already out of its gzip).
struct Layout {
    uint32_t version = 0;
    size_t start = 0;        // the first command
    size_t loop = 0;         // where it loops back to, 0 if it does not
    uint64_t samples = 0;    // its length in 44.1 kHz ticks, without repeating
    uint64_t loopSamples = 0;  // of which the looped part
    std::string title, game;   // from its GD3 tag
};
// Whether `d` starts as a VGM does (its "Vgm " and a whole header), and its version.
bool isVgm(const std::vector<uint8_t>& d, uint32_t* version);
// The rest of the header and the tag; false (and why, in Spanish) if it is cut short.
bool readLayout(const std::vector<uint8_t>& d, Layout* out, std::string* error);

}  // namespace vgm
}  // namespace plum
