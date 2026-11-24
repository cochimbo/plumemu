#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "core/mp3_encode.h"

#include "roland.h"

namespace plumsg {

struct VgmFile;

// Exportar a MP3: a song as the instrument editor plays it (the Roland sound with what was
// chosen for it, and the chip's own sound in the instruments left original), to a file to
// keep or share. As plumnin does.
struct SongRender {
    int rate = 44100;
    int loops = 2;            // times round its loop, after its start
    double fadeSeconds = 8;   // then it fades out (a song that ends just ends)
    double maxSeconds = 600;  // the longest it can be, fade included
};

// How long the export of `file` lasts, its fade included.
double renderedSeconds(const VgmFile& file, const SongRender& how);
// Plays `file` through `roland` (null: the chips' own sound) into `out` (interleaved
// stereo at how.rate). With `roland`, the whole song is heard first, so it knows every
// instrument from its first note, and then plays them with what was chosen for the whole
// game (`game`, if given) and, over it, for this song alone (`song`). `progress`, if given, goes from 0 to 1; `cancel` stops it (false is returned).
bool renderSong(const VgmFile& file, RolandSound* roland, const RolandSound::Assignments* game,
                const RolandSound::Assignments* song, const SongRender& how,
                std::vector<int16_t>* out, std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

// (encodeMp3 and Mp3Tag: core/mp3_encode.h, in plum.)

}  // namespace plumsg
