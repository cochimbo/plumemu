#pragma once

// General MIDI, as both programs' synthesizers play the games' music with it (plumsg's
// RolandSound, plumnin's SynthMusic): the instruments and drums by name, what each of the
// game's instruments is played as, and the file those choices are kept in.

#include <cstdint>
#include <map>
#include <string>

#include "plum.h"

namespace plum {
namespace gm {

// The equalizer: a gain in dB (-kEqRange..kEqRange) for each band, from the bass up.
inline constexpr int kEqBands = 5;
inline constexpr int kEqRange = 12;
inline constexpr double kEqFrequencies[kEqBands] = {100, 400, 1200, 3500, 9000};

// What one of the game's instruments (by its fingerprint) is played as.
struct Assignment {
    int program = -1;       // General MIDI instrument 0-127
    int drum = -1;          // or a piece of the GM drum kit (note 35-81), on the drum channel
    bool original = false;  // or none: it keeps its own sound from the chip
    int octave = 0;         // -3..3, added to every note
    int volume = 100;       // percent of the note's own loudness
    bool operator==(const Assignment& o) const {
        return program == o.program && drum == o.drum && original == o.original && octave == o.octave &&
               volume == o.volume;
    }
    bool operator!=(const Assignment& o) const { return !(*this == o); }
};
using Assignments = std::map<uint32_t, Assignment>;

struct Synth;  // core/gm_synth.h

// Their names, translated: "" for a program out of 0-127, nullptr for a drum out of 35-81.
const char* programName(int program);
const char* drumName(int note);

// Assignment files: one line per instrument, "fingerprint = what [octave volume]", what
// being a program (0-127), "bateria N" or "original". Lines that make no sense are
// skipped. formatAssignments() starts with `heading` (comment lines) and, with
// `withNames`, ends each line with the instrument's name as a comment.
Assignments parseAssignments(const std::string& text);
std::string formatAssignments(const Assignments& assignments, const char* heading, bool withNames);

}  // namespace gm
}  // namespace plum
