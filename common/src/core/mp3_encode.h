#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace plum {

// The MP3 of Exportar a MP3 (shine, fixed point), with its ID3 tag.
struct Mp3Tag {
    std::string title, album, artist;
    int track = 0;  // 0 for none
};
// Interleaved stereo at `rate` (44100, 48000 or 32000) to an MP3 at `kbps`, with an
// ID3 tag. Empty if the rate or the bit rate is not one MP3 has.
std::vector<uint8_t> encodeMp3(const std::vector<int16_t>& stereo, int rate, int kbps, const Mp3Tag& tag);

}  // namespace plum
