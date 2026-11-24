#include "synth_music.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/i18n.h"

#include "core/gm_synth.h"

namespace plumnin {

namespace {

constexpr int kBendRange = 12;  // semitones either way, so slides stay one note
constexpr double kDyingHalf = 0.65;    // seconds: a note's envelope halving this fast is dying away
constexpr double kSettle = 0.02;       // seconds after its peak: past the attack and the fall to the sustain
constexpr double kEffectHits = 0.035;  // seconds: noise struck again sooner goes on as one sound



// The harmonics of a wave of 32 samples: how much of its energy is in the fundamental,
// and in the odd harmonics (a square or a clarinet has only those).
struct WaveShape {
    double fundamental = 1, odd = 1;
};
WaveShape waveShape(const std::array<uint8_t, 32>& wave) {
    double mean = 0;
    for (uint8_t s : wave) mean += s;
    mean /= 32;
    double total = 0, first = 0, odd = 0;
    for (int h = 1; h <= 16; ++h) {
        double re = 0, im = 0;
        for (int i = 0; i < 32; ++i) {
            const double a = 2 * 3.14159265358979 * h * i / 32;
            re += (wave[static_cast<size_t>(i)] - mean) * std::cos(a);
            im -= (wave[static_cast<size_t>(i)] - mean) * std::sin(a);
        }
        const double e = re * re + im * im;
        total += e;
        if (h == 1) first = e;
        if (h & 1) odd += e;
    }
    if (total <= 0) return {};
    return {first / total, odd / total};
}

}  // namespace


SynthMusic::SynthMusic() : synth_(std::make_unique<gm::Synth>()) {}

SynthMusic::~SynthMusic() = default;

uint32_t SynthMusic::hash(const uint8_t* data, size_t size, uint32_t seed) {
    uint32_t h = seed;
    for (size_t i = 0; i < size; ++i) h = (h ^ data[i]) * 16777619u;
    return h;
}

bool SynthMusic::loadSoundfont(const std::vector<uint8_t>& data) {
    tsf* font = data.empty() ? nullptr : tsf_load_memory(data.data(), static_cast<int>(data.size()));
    if (!font) return false;
    if (synth_->font) tsf_close(synth_->font);
    synth_->font = font;
    fontKey_ = font;
    synth_->rate = 0;
    for (Voice& v : voices_) v.on = v.midi = false;
    channels_ = {};
    return true;
}

bool SynthMusic::ready() const { return synth_->font != nullptr; }

bool SynthMusic::shareSoundfont(const SynthMusic& from) {
    if (!from.synth_->font) return false;
    if (fontKey_ == from.fontKey_ && synth_->font) return true;
    tsf* font = tsf_copy(from.synth_->font);
    if (!font) return false;
    if (synth_->font) tsf_close(synth_->font);
    synth_->font = font;
    fontKey_ = from.fontKey_;
    synth_->rate = 0;
    for (Voice& v : voices_) v.on = v.midi = false;
    channels_ = {};
    return true;
}

void SynthMusic::setEffects(int reverb, int chorus) { synth_->effects.setAmounts(reverb, chorus); }

void SynthMusic::setEqualizer(const std::array<int, kEqBands>& gains) {
    std::array<int, kEqBands> clamped = gains;
    for (int& g : clamped) g = std::clamp(g, -kEqRange, kEqRange);
    synth_->equalizer.setGains(clamped);
}

const char* SynthMusic::programName(int program) { return gm::programName(program); }

const char* SynthMusic::drumName(int note) { return gm::drumName(note); }

double SynthMusic::noteOf(double hz) { return hz > 0 ? 69.0 + 12.0 * std::log2(hz / 440.0) : 0; }

void SynthMusic::Heard::add(int note) {
    ++notes;
    ++pitches[static_cast<size_t>(std::clamp(note, 0, 127))];
}

int SynthMusic::Heard::distinct() const {
    int count = 0;
    for (uint16_t p : pitches) count += p != 0;
    return count;
}

int SynthMusic::Heard::span() const {
    int low = -1, high = -1;
    for (int n = 0; n < 128; ++n)
        if (pitches[static_cast<size_t>(n)]) {
            if (low < 0) low = n;
            high = n;
        }
    return low < 0 ? 0 : high - low;
}

int SynthMusic::Heard::median() const {
    int count = 0;
    for (int n = 0; n < 128; ++n)
        if ((count += pitches[static_cast<size_t>(n)]) * 2 >= notes) return n;
    return 60;
}

// The rules, until an instrument is chosen by hand. The noise is the drums: the higher
// it is, the smaller the drum (bass drum, snare, hi-hat), and a long one is a cymbal.
// The tones are an orchestra: in the bass register a bass; a note that dies away by
// itself plucked (a guitar or a harp); otherwise by the wave's harmonics, a pulse being
// thinner the narrower it is (a 12.5% oboe, a 25% trumpet, a 50% square a clarinet), a
// wave with nearly only its fundamental a flute, and the NES triangle a flute too.
SynthMusic::Assignment SynthMusic::suggest(const Traits& t, const Heard& heard) {
    Assignment a;
    const double held = heard.held ? heard.heldSeconds / heard.held : 0.1;
    if (t.kind == Kind::Noise) {
        if (t.noise >= 11) a.drum = held > 0.5 ? 49 : held > 0.2 ? 46 : 42;
        else if (t.noise >= 6) a.drum = 38;
        else a.drum = 36;
        return a;
    }
    const int median = heard.notes ? heard.median() : 64;
    const bool plucked = heard.held >= 3 && heard.decayed * 3 >= heard.held * 2;
    switch (t.kind) {
        case Kind::Triangle: a.program = median < 55 ? 33 : 73; break;
        case Kind::Wave: {
            const WaveShape w = waveShape(t.wave);
            if (median < 50) a.program = w.fundamental > 0.8 ? 33 : 38;
            else if (plucked) a.program = 46;
            else if (w.fundamental > 0.85) a.program = 73;
            else if (w.odd > 0.9) a.program = 71;
            else a.program = 48;
            break;
        }
        case Kind::Sample: {
            // A sample without a pitch, or a short one the game plays on one or two
            // notes, is a drum, by how bright it is; a bright noise that loops a cymbal,
            // by how long the game holds it.
            // (Before four notes, only a very short one: a short note with a clear pitch may yet be
            // a piano, and a melody's first notes taken for a bass drum come out as a roll.)
            // A very short dark one with a pitch is a tom or a bongo, however many notes the
            // game plays it on (Super Mario World's, Yoshi's drums): no tune is played on a
            // knock, and as a marimba or a piano it is a tune where there was none.
            // (A little brighter too, played within a few notes: Donkey Kong Country's jungle
            // drums, on four notes, came out as a bass.)
            if (t.pitched && !t.looped && t.seconds < 0.15 &&
                (t.brightness < 0.06 || (t.brightness < 0.1 && heard.notes >= 4 && heard.span() <= 5))) {
                a.drum = median >= 55 ? 60 : median >= 45 ? 64 : 45;
                break;
            }
            // One with no clear pitch that the game plays on many notes, over an octave or
            // more, is a tune all the same, of a rough sound: a bass, a brass, not a hi-hat
            // (Star Fox's in Corneria). (Not a click too short to hold a note.)
            if (!t.pitched && t.roughHz > 0 && t.seconds >= 0.05 && heard.notes >= 8 && heard.distinct() >= 10 &&
                heard.span() >= 12) {
                a.program = median < 48 ? 38 : t.brightness > 0.3 ? 62 : t.looped ? 48 : 12;
                break;
            }
            const bool drum = !t.pitched || (!t.looped && t.seconds < 0.35 &&
                                             (heard.notes < 4 ? t.seconds < 0.15 : heard.distinct() <= 2));
            if (drum) {
                if (!t.pitched && t.looped && t.brightness >= 0.18) a.drum = held < 0.25 ? 42 : held < 0.6 ? 46 : 49;
                else a.drum = t.brightness < 0.06 ? 36 : t.brightness < 0.18 ? 38 : t.seconds > 0.6 ? 49 : 42;
                // A hiss the game plays well below the note it was recorded at sounds darker
                // than it is, a soft "chh": a pedal hi-hat, not the closed one's sharp tick,
                // which is far brighter (Donkey Kong Country's in its first level, played a
                // fifth down; the same sample played up in another song stays a closed one).
                if (a.drum == 42 && !t.pitched && t.roughHz > 0 && heard.notes) {
                    const double played = t.brightness * std::exp2((median - noteOf(t.roughHz)) / 12.0);
                    if (played < 0.6) a.drum = 44;
                }
                break;
            }
            // The rest by how it holds its note, with the chip's envelope: one that dies
            // away is struck or plucked, one that swells bowed or sung, and one that holds
            // by its harmonics.
            const bool swells = heard.held >= 3 && heard.swelled * 2 >= heard.held;
            if (median < 48) {
                if (!t.looped) a.program = 32;
                else a.program = !plucked && t.brightness > 0.25 ? 38 : 33;
            } else if (!t.looped) {
                // A short one is a marimba, but over two octaves or more (the bass, the
                // chords and the tune of a song all on it) a piano.
                a.program = t.seconds < 0.5 && heard.span() < 24 ? 12 : median < 62 && t.seconds >= 0.5 ? 24 : 0;
            } else if (plucked) {
                if (median >= 79) a.program = t.fundamental > 0.8 ? 10 : 8;
                else if (t.fundamental > 0.8) a.program = 46;
                else if (median < 60) a.program = t.brightness > 0.15 ? 25 : 24;
                else a.program = 0;
            } else if (swells) {
                a.program = t.fundamental > 0.8 ? 52 : t.brightness > 0.25 ? 61 : 48;
            } else if (t.fundamental > 0.8) {
                a.program = median >= 64 ? 73 : 71;
            } else if (t.odd > 0.8) {
                a.program = 71;
            } else if (t.brightness > 0.25) {
                a.program = 61;
            } else {
                a.program = 48;
            }
            break;
        }
        default: {
            static constexpr int kByDuty[4] = {68, 56, 71, 56};
            if (median < 45) a.program = 38;
            else if (plucked) a.program = median < 62 ? 24 : 46;
            else a.program = kByDuty[t.duty & 3];
            break;
        }
    }
    return a;
}

SynthMusic::Assignment SynthMusic::assignmentFor(uint32_t fingerprint) const {
    if (song_ >= 0 && static_cast<size_t>(song_) < songs_.size()) {
        const Assignments& own = songs_[static_cast<size_t>(song_)].assigned;
        if (auto a = own.find(fingerprint); a != own.end()) return a->second;
    }
    if (auto a = assigned_.find(fingerprint); a != assigned_.end()) return a->second;
    return suggestionFor(fingerprint);
}

void SynthMusic::setSoundEffects(std::set<uint32_t> effects, bool original) {
    effects_ = std::move(effects);
    effectsOriginal_ = original;
    updateChipVoices();
}

SynthMusic::Assignment SynthMusic::suggestionFor(uint32_t fingerprint) const {
    if (effectsOriginal_ && effects_.count(fingerprint)) {
        Assignment original;
        original.original = true;
        return original;
    }
    return ruled(fingerprint);
}

// What the rules say, of the instrument or of the one it follows.
SynthMusic::Assignment SynthMusic::ruled(uint32_t fingerprint) const {
    auto r = rules_.find(fingerprint);
    if (r == rules_.end()) {
        Traits t;
        t.kind = kindOf(fingerprint);
        return suggest(t, Heard{});
    }
    if (r->second.lead)
        if (auto l = rules_.find(r->second.lead); l != rules_.end()) return suggest(l->second.traits, l->second.heard);
    return suggest(r->second.traits, r->second.heard);
}

void SynthMusic::setSongs(std::vector<Song> songs, bool keepAsked) {
    // The song asked for stays the one playing, when it is among them still.
    std::string request;
    int number = -1;
    if (keepAsked && asked_ && song_ >= 0 && static_cast<size_t>(song_) < songs_.size()) {
        request = songs_[static_cast<size_t>(song_)].request;
        number = songs_[static_cast<size_t>(song_)].number;
    }
    songs_ = std::move(songs);
    song_ = -1;
    asked_ = false;
    guessedAt_ = 0;
    if (!request.empty()) playingSong(request, number);
    if (!asked_) guessSong(guessedAt_);
    updateChipVoices();
}

void SynthMusic::playingSong(const std::string& request, int number) {
    for (size_t i = 0; i < songs_.size(); ++i) {
        if (songs_[i].request != request || songs_[i].number != number) continue;
        asked_ = true;
        if (song_ != static_cast<int>(i)) {
            song_ = static_cast<int>(i);
            updateChipVoices();
        }
        return;
    }
}

// The song whose instruments are the ones heard in the last seconds: the one with most of
// them (at least most of them all), and of those, the one they are most of. A song the
// heard ones fit as well as the one before stays.
void SynthMusic::guessSong(uint64_t clock) {
    guessedAt_ = clock;
    if (asked_) return;  // the game said which
    if (songs_.empty()) {
        song_ = -1;
        return;
    }
    const uint64_t window = static_cast<uint64_t>(6 * clockHz_);
    std::set<uint32_t> heard;
    for (auto it = recent_.begin(); it != recent_.end();) {
        if (clock > it->second && clock - it->second > window) {
            it = recent_.erase(it);
            continue;
        }
        if (!effects_.count(it->first)) heard.insert(it->first);
        ++it;
    }
    if (heard.empty()) return;  // a silence: the song it was, until another is heard
    int best = -1;
    double bestScore = 0;
    double currentScore = -1;
    for (size_t i = 0; i < songs_.size(); ++i) {
        const std::set<uint32_t>& in = songs_[i].instruments;
        if (in.empty()) continue;
        size_t both = 0;
        for (uint32_t fp : heard) both += in.count(fp);
        const double ofHeard = static_cast<double>(both) / static_cast<double>(heard.size());
        if (ofHeard < 0.6) continue;
        const double score = ofHeard * 2 + static_cast<double>(both) / static_cast<double>(in.size());
        if (static_cast<int>(i) == song_) currentScore = score;
        if (score > bestScore + 1e-9) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    const int was = song_;
    song_ = currentScore >= bestScore - 1e-9 && currentScore > 0 ? song_ : best;
    if (song_ != was) updateChipVoices();
}

void SynthMusic::setAssignments(Assignments assigned) {
    assigned_ = std::move(assigned);
    updateChipVoices();
}

void SynthMusic::updateChipVoices() {
    chipVoices_ = 0;
    silentVoices_ = mutedVoices_;
    for (int i = 0; i < kVoices; ++i) {
        const Voice& v = voices_[static_cast<size_t>(i)];
        if (v.fingerprint && assignmentFor(v.fingerprint).original) chipVoices_ |= 1u << i;
        if (v.fingerprint && silenced_.count(v.fingerprint)) silentVoices_ |= 1u << i;
    }
}

void SynthMusic::setListening(uint32_t mutedVoices, std::set<uint32_t> silenced) {
    mutedVoices_ = mutedVoices;
    silenced_ = std::move(silenced);
    // What sounds and should not stops now.
    for (int i = 0; i < kVoices; ++i) {
        Voice& v = voices_[static_cast<size_t>(i)];
        if (v.midi && ((mutedVoices_ >> i) & 1 || silenced_.count(v.fingerprint))) {
            if (!v.drum) emit(frameStart_, Ev::NoteOff, i, v.note);
            v.midi = false;
        }
    }
    updateChipVoices();
}

uint32_t SynthMusic::activeVoices() const {
    uint32_t mask = 0;
    for (int i = 0; i < kVoices; ++i)
        if (voices_[static_cast<size_t>(i)].on) mask |= 1u << i;
    return mask;
}

SynthMusic::Assignments SynthMusic::parseAssignments(const std::string& text) { return gm::parseAssignments(text); }

std::string SynthMusic::formatAssignments(const Assignments& assignments) {
    return gm::formatAssignments(assignments,
                                 "; Instrumentos del sintetizador: huella = instrumento GM (0-127) octava volumen,\n"
                                 "; bateria nota 0 volumen (una pieza del kit, 35-81), u original.\n[instrumentos]\n",
                                 false);
}

void SynthMusic::emit(uint64_t clock, Ev type, int channel, int value, int extra) {
    events_.push_back({clock, type, static_cast<uint8_t>(channel), value, extra});
    if (!log_) return;
    static const char* kNames[] = {"on", "off", "program", "bend", "pan", "expression"};
    char line[64];
    std::snprintf(line, sizeof line, "%d %s %d", channel, kNames[static_cast<int>(type)], value);
    log_->push_back(line);
}

void SynthMusic::beginFrame(uint64_t clock) { frameStart_ = clock; }

void SynthMusic::allNotesOff() {
    events_.clear();
    for (Voice& v : voices_) v.on = v.midi = false;
    channels_ = {};
    if (synth_->font && synth_->rate) {
        tsf_note_off_all(synth_->font);
        for (int ch = 0; ch < 16; ++ch) {
            tsf_channel_set_pitchwheel(synth_->font, ch, 8192);
            tsf_channel_set_pan(synth_->font, ch, 0.5f);
            tsf_channel_midi_control(synth_->font, ch, 11, 127);
        }
    }
}

void SynthMusic::forget() {
    allNotesOff();
    recent_.clear();
    song_ = -1;
    asked_ = false;
    voices_ = {};
    rules_.clear();
    seen_.clear();
    updateChipVoices();
}

void SynthMusic::learn(const std::map<uint32_t, Seen>& heard) {
    for (const auto& [fp, s] : heard) {
        Rule& rule = rules_[fp];
        rule.traits = s.traits;
        rule.heard = s.heard;
    }
}

// How much sound a drum sample holds, struck once at full volume (Traits::strike): a
// snare's, a tom's, a hi-hat's of the SNES, as loud as the SoundFont's.
static constexpr double kDrumStrike = 0.003;

int SynthMusic::velocity(const Voice& v, double amplitude) {
    return std::clamp(static_cast<int>(std::lround(127 * std::sqrt(amplitude) * v.volume / 100.0)), 1, 127);
}

void SynthMusic::noteOn(int voice, const VoiceState& s, uint64_t clock) {
    noteOff(voice, clock);
    Voice& v = voices_[static_cast<size_t>(voice)];
    const uint32_t fp = s.fingerprint;
    // What the rules go by, and what the editor shows.
    Rule& rule = rules_[fp];
    rule.traits.kind = kindOf(fp);
    rule.traits.duty = s.duty;
    rule.traits.noise = s.noise;
    if (s.wave) std::copy(s.wave, s.wave + 32, rule.traits.wave.begin());
    if (s.traits) {
        rule.traits = *s.traits;
        rule.traits.kind = kindOf(fp);
    }
    // A channel of the tune (a pulse or a wave: the chips' samples are each an
    // instrument) that changes its sound for some notes plays on as the same instrument.
    // A tune is the channel's notes with no more than five seconds between them.
    if ((rule.traits.kind == Kind::Pulse || rule.traits.kind == Kind::Wave) && voice < 8) {
        Lead& lead = leads_[static_cast<size_t>(voice)];
        const bool fresh = rule.heard.notes == 0;
        const auto l = rules_.find(lead.fingerprint);
        if (!lead.fingerprint || l == rules_.end() || l->second.traits.kind != rule.traits.kind ||
            static_cast<double>(clock - lead.last) > 5 * clockHz_)
            lead.fingerprint = rule.lead ? rule.lead : fp;
        else if (fresh && lead.fingerprint != fp)
            rule.lead = lead.fingerprint;
        lead.last = clock;
    }
    // Which song it is, by its instruments: before choosing this one's sound.
    if (!songs_.empty()) {
        const bool fresh = !recent_.count(fp);
        recent_[fp] = clock;
        if (fresh || clock < guessedAt_ || static_cast<double>(clock - guessedAt_) > 0.25 * clockHz_) guessSong(clock);
    }
    const int pitch = static_cast<int>(std::lround(s.note));
    if (rule.traits.kind != Kind::Noise) rule.heard.add(pitch);
    else ++rule.heard.notes;
    const Assignment a = assignmentFor(fp);
    if (!seen_.count(fp)) seen_[fp].order = static_cast<int>(seen_.size()) - 1;
    Seen& seen = seen_[fp];
    seen.kind = rule.traits.kind;
    seen.traits = rule.traits;
    seen.voices |= 1u << voice;
    ++seen.notes;
    seen.low = std::min(seen.low, pitch);
    seen.high = std::max(seen.high, pitch);
    seen.heard = rule.heard;
    seen.rules = ruled(fp);

    v.on = true;
    v.amplitude = std::max(s.amplitude, 1e-3);
    v.lowestRatio = 1;
    v.start = v.peakAt = clock;
    v.peakEnvelope = s.envelope;
    v.refEnvelope = -1;
    v.refAt = v.lastAt = clock;
    if (v.fingerprint != fp) {
        v.fingerprint = fp;
        updateChipVoices();
    }
    v.drum = a.drum >= 0;
    const bool quiet = a.original || ((mutedVoices_ >> voice) & 1) || silenced_.count(fp);
    // The noise only plays drums.
    v.midi = !quiet && (v.drum || (a.program >= 0 && rule.traits.kind != Kind::Noise));
    if (!v.midi) return;
    v.volume = a.volume;
    if (v.drum) {
        v.note = a.drum;
        // A sample that holds much less sound than a drum's, a click of a hundredth of a
        // second, struck as loud as a whole drum is far louder than on the console: its
        // drum as much softer (Donkey Kong Country's jungle hi-hat, over the whole song).
        double strike = 1;
        if (rule.traits.kind == Kind::Sample && !rule.traits.looped && rule.traits.strike > 0)
            strike = std::min(1.0, std::sqrt(rule.traits.strike / kDrumStrike));
        emit(clock, Ev::NoteOn, kDrums, v.note, velocity(v, v.amplitude * strike));
        return;
    }
    Channel& c = channels_[static_cast<size_t>(voice)];
    if (a.program != c.program) {
        c.program = a.program;
        emit(clock, Ev::Program, voice, a.program);
    }
    panTo(voice, s.pan, clock);
    if (c.expression != 127) {
        c.expression = 127;
        emit(clock, Ev::Expression, voice, 127);
    }
    v.octave = 12 * a.octave;
    v.note = std::clamp(pitch + v.octave, 0, 127);
    pitchTo(voice, s.note, clock);
    emit(clock, Ev::NoteOn, voice, v.note, velocity(v, v.amplitude));
}

void SynthMusic::noteOff(int voice, uint64_t clock) {
    Voice& v = voices_[static_cast<size_t>(voice)];
    if (v.on) {
        if (auto r = rules_.find(v.fingerprint); r != rules_.end()) {
            Heard& h = r->second.heard;
            ++h.held;
            if (clock > v.start) h.heldSeconds += static_cast<double>(clock - v.start) / clockHz_;
            // Died away: below a third. With the chip's envelope, also a short note let go
            // while dying away, from a moment after its attack (and the fall to its sustain,
            // which a held note has too) halving in two thirds of a second or less: a
            // struck note played short, as a melody is, never gets down to a third.
            bool decayed = v.lowestRatio < 0.34;
            if (!decayed && v.refEnvelope > 0 && v.lastAt > v.refAt) {
                const double since = static_cast<double>(v.lastAt - v.refAt) / clockHz_;
                const double ratio = v.lastEnvelope / v.refEnvelope;
                decayed = since >= 0.01 && ratio < 0.97 && ratio <= std::pow(0.5, since / kDyingHalf);
            }
            if (decayed) ++h.decayed;
            if (v.peakEnvelope >= 0 && static_cast<double>(v.peakAt - v.start) > 0.1 * clockHz_) ++h.swelled;
        }
    }
    // Drums ring out by themselves, as on a GM module.
    if (v.on && v.midi && !v.drum) emit(clock, Ev::NoteOff, voice, v.note);
    v.on = v.midi = false;
}

void SynthMusic::panTo(int voice, int pan, uint64_t clock) {
    Voice& v = voices_[static_cast<size_t>(voice)];
    v.pan = pan;
    if (!v.midi || v.drum) return;
    Channel& c = channels_[static_cast<size_t>(voice)];
    if (pan == c.pan) return;
    c.pan = pan;
    emit(clock, Ev::Pan, voice, pan);
}

void SynthMusic::expression(int voice, double amplitude, uint64_t clock) {
    Voice& v = voices_[static_cast<size_t>(voice)];
    if (v.on && v.peakEnvelope < 0) v.lowestRatio = std::min(v.lowestRatio, amplitude / v.amplitude);
    if (!v.midi || v.drum) return;
    Channel& c = channels_[static_cast<size_t>(voice)];
    const int e = std::clamp(static_cast<int>(127 * std::sqrt(amplitude / v.amplitude)), 0, 127);
    if (e == c.expression) return;
    c.expression = e;
    emit(clock, Ev::Expression, voice, e);
}

void SynthMusic::pitchTo(int voice, double note, uint64_t clock) {
    Voice& v = voices_[static_cast<size_t>(voice)];
    if (!v.midi || v.drum || note <= 0) return;
    note += v.octave;
    Channel& c = channels_[static_cast<size_t>(voice)];
    // Within the bend range the note bends; further, it is played again at the new note.
    if (std::fabs(note - v.note) > kBendRange) {
        emit(clock, Ev::NoteOff, voice, v.note);
        v.note = std::clamp(static_cast<int>(std::lround(note)), 0, 127);
        emit(clock, Ev::NoteOn, voice, v.note, velocity(v, v.amplitude));
    }
    const int bend = std::clamp(8192 + static_cast<int>(std::lround((note - v.note) * 8192.0 / kBendRange)), 0, 16383);
    if (bend != c.bend) {
        c.bend = bend;
        emit(clock, Ev::Bend, voice, bend);
    }
}

// The note of a sample: YIN's difference function (de Cheveigné and Kawahara) over a
// stretch of it, the loop repeated if it loops, which finds the period that best repeats.
// Where nothing repeats well it has no pitch. Then how bright it is (its spectrum's
// centre) and how much of it is the fundamental, from the harmonics of that period.
double SynthMusic::analyzeSample(const int16_t* pcm, size_t count, size_t loopStart, bool looped, double rate,
                                 Traits* traits) {
    Traits t;
    t.kind = Kind::Sample;
    t.looped = looped && loopStart < count;
    t.seconds = t.looped ? 10.0 : static_cast<double>(count) / rate;
    if (!t.looped) {
        double sum = 0;
        for (size_t i = 0; i < count; ++i) sum += (pcm[i] / 32768.0) * (pcm[i] / 32768.0);
        t.strike = sum / rate;
    }
    constexpr size_t kLength = 2048, kMaxLag = 1000, kMinLag = 8;
    std::vector<double> x(kLength, 0.0);
    if (t.looped) {
        const size_t loop = count - loopStart;
        for (size_t i = 0; i < kLength; ++i) {
            const size_t at = loopStart + (i < count - loopStart ? i : i % loop);
            x[i] = at < count ? pcm[at] : 0;
        }
        // The stretch before the loop too, when there is enough of it: the attack.
        if (loopStart >= kLength + count / 10)
            for (size_t i = 0; i < kLength; ++i) x[i] = pcm[count / 10 + i];
    } else {
        const size_t from = std::min(count / 10, count > kLength ? count - kLength : 0);
        for (size_t i = 0; i < kLength && from + i < count; ++i) x[i] = pcm[from + i];
    }
    double energy = 0;
    for (double v : x) energy += v * v;
    double hz = 0;
    // (A looped one however short: a single cycle of a wave, Jungle Hijinxs' whistle of 48
    // samples, has its note all the same, and with none it was a hi-hat.)
    if (energy > 1e3 && (count >= 64 || t.looped)) {
        const size_t window = kLength - kMaxLag;
        std::vector<double> d(kMaxLag + 1, 0.0);
        double running = 0;
        size_t best = 0;
        double bestValue = 1e9;
        for (size_t lag = 1; lag <= kMaxLag; ++lag) {
            double sum = 0;
            for (size_t j = 0; j < window; ++j) {
                const double diff = x[j] - x[j + lag];
                sum += diff * diff;
            }
            running += sum;
            d[lag] = running > 0 ? sum * static_cast<double>(lag) / running : 1;
        }
        for (size_t lag = kMinLag; lag <= kMaxLag; ++lag) {
            if (d[lag] < 0.15) {
                while (lag + 1 <= kMaxLag && d[lag + 1] < d[lag]) ++lag;
                best = lag;
                bestValue = d[lag];
                break;
            }
            if (d[lag] < bestValue) best = lag, bestValue = d[lag];
        }
        if (best && bestValue < 0.35) {
            double lag = static_cast<double>(best);
            if (best > kMinLag && best < kMaxLag) {  // between samples, by a parabola
                const double a = d[best - 1], b = d[best], c = d[best + 1];
                const double den = a - 2 * b + c;
                if (den > 0) lag += 0.5 * (a - c) / den;
            }
            hz = rate / lag;
        } else if (best && bestValue < 0.8) {
            t.roughHz = rate / static_cast<double>(best);
        }
    }
    t.pitched = hz > 0;
    // The spectrum, by its DFT at a few hundred frequencies, for the brightness; and the
    // harmonics of the note for the fundamental's part.
    if (energy > 0) {
        double centre = 0, total = 0;
        for (int k = 1; k < 128; ++k) {
            const double f = k / 256.0;  // of the rate
            double re = 0, im = 0;
            // (Every sample: taking every other one folds the top half of the spectrum
            // onto the bottom one, and every sound came out exactly half bright.)
            for (size_t i = 0; i < kLength; ++i) {
                const double a = 2 * 3.14159265358979 * f * static_cast<double>(i);
                re += x[i] * std::cos(a);
                im -= x[i] * std::sin(a);
            }
            const double e = re * re + im * im;
            centre += e * k / 128.0;
            total += e;
        }
        t.brightness = total > 0 ? centre / total : 0;
        if (hz > 0) {
            double first = 0, all = 0, odd = 0;
            for (int h = 1; h <= 12 && h * hz < rate / 2; ++h) {
                double re = 0, im = 0;
                for (size_t i = 0; i < kLength; ++i) {
                    const double a = 2 * 3.14159265358979 * h * hz * static_cast<double>(i) / rate;
                    re += x[i] * std::cos(a);
                    im -= x[i] * std::sin(a);
                }
                const double e = re * re + im * im;
                if (h == 1) first = e;
                else if (h % 2) odd += e;
                all += e;
            }
            t.fundamental = all > 0 ? first / all : 0;
            t.odd = all > first ? odd / (all - first) : 0.5;
        }
    }
    if (traits) *traits = t;
    return hz;
}

// A new note is a key on, another instrument, or, as the Nintendo chips are often
// played without a key on for each note (the NES drivers that leave the high byte of
// the period alone so as not to click), the pitch jumping to another semitone at once
// or the loudness jumping up. A vibrato or a slide moves the pitch bend.
void SynthMusic::update(int voice, const VoiceState& s, uint64_t clock) {
    if (voice < 0 || voice >= kVoices) return;
    Voice& v = voices_[static_cast<size_t>(voice)];
    const bool triggered = s.triggers != v.triggers;
    v.triggers = s.triggers;
    // The chip's envelope, where it has one: how long the note took to swell, and whether
    // it then died away (also when it dies out and the note ends with it, but not as it is
    // let go: the release takes it down in a moment, a held note as much as a struck one).
    if (v.on && !triggered && v.peakEnvelope >= 0 && s.envelope >= 0 && (s.sounding || s.envelope == 0)) {
        if (s.envelope > v.peakEnvelope) {
            v.peakEnvelope = s.envelope;
            v.peakAt = clock;
            v.refEnvelope = -1;
        } else if (v.peakEnvelope > 0) {
            v.lowestRatio = std::min(v.lowestRatio, s.envelope / v.peakEnvelope);
            if (v.refEnvelope < 0 && static_cast<double>(clock - v.peakAt) >= kSettle * clockHz_) {
                v.refEnvelope = s.envelope;
                v.refAt = clock;
            }
        }
        v.lastEnvelope = s.envelope;
        v.lastAt = clock;
    }
    const bool noise = kindOf(s.fingerprint) == Kind::Noise;
    const bool audible = s.sounding && s.amplitude > 0 && s.fingerprint && (noise || (s.note > 12 && s.note < 120));
    if (!audible) {
        noteOff(voice, clock);
        v.lastAmplitude = 0;
        v.lastNote = 0;
        return;
    }
    bool fresh = !v.on || triggered || s.fingerprint != v.fingerprint;
    if (!fresh && !s.keyed && s.amplitude > v.lastAmplitude * 1.9 && s.amplitude >= 0.1) fresh = true;
    if (!fresh && !s.keyed && !noise && std::fabs(s.note - v.lastNote) >= 0.9) fresh = true;
    v.lastNote = s.note;
    v.lastAmplitude = s.amplitude;
    // A noise struck again within two frames of the last time is the same sound going
    // on: the drivers step their drums, and sweep their sound effects (a sword, an
    // explosion), frame by frame. It is one drum, not a roll of them.
    if (fresh && noise) {
        const bool rapid = v.lastHit && clock > v.lastHit && static_cast<double>(clock - v.lastHit) < kEffectHits * clockHz_;
        v.lastHit = clock;
        if (rapid) {
            if (s.fingerprint != v.fingerprint) {
                v.fingerprint = s.fingerprint;
                updateChipVoices();
            }
            v.on = true;
            return;
        }
    }
    if (fresh) {
        noteOn(voice, s, clock);
        return;
    }
    if (!noise) pitchTo(voice, s.note, clock);
    expression(voice, s.amplitude, clock);
    if (s.pan != v.pan) panTo(voice, s.pan, clock);
}

void SynthMusic::apply(const Event& e) {
    tsf* f = synth_->font;
    const int ch = e.channel;
    switch (e.type) {
        case Ev::NoteOn:
            // A drum struck again starts over, as a real one would: the drums are never let
            // go, and some of the SoundFont's pieces take seconds to die away once struck
            // (its toms), so struck every beat they piled up until the synth had no voices
            // left and the song went quiet (Jungle Hijinxs, after half a minute).
            if (ch == kDrums) gm::endKeyQuick(f, ch, e.value);
            tsf_channel_note_on(f, ch, e.value, static_cast<float>(e.extra) / 127.0f);
            break;
        case Ev::NoteOff: tsf_channel_note_off(f, ch, e.value); break;
        case Ev::Program: tsf_channel_set_presetnumber(f, ch, e.value, ch == kDrums); break;
        case Ev::Bend: tsf_channel_set_pitchwheel(f, ch, e.value); break;
        case Ev::Pan: tsf_channel_set_pan(f, ch, static_cast<float>(e.value) / 127.0f); break;
        case Ev::Expression: tsf_channel_midi_control(f, ch, 11, e.value); break;
    }
}

void SynthMusic::render(int16_t* out, int frames) {
    if (frames <= 0) return;
    std::vector<float>& buf = synth_->buffer;
    buf.assign(static_cast<size_t>(frames) * 2, 0.0f);
    tsf_render_float(synth_->font, buf.data(), frames, 0);
    synth_->effects.process(buf.data(), frames);
    synth_->equalizer.process(buf.data(), frames);
    // As loud as the chip's own sound of the same music, more or less; the peaks above
    // three quarters of full scale are rounded off rather than clipped.
    const float gain = 20000.0f * static_cast<float>(volume_) / 100.0f;
    constexpr float kKnee = 24000.0f, kRoom = 32767.0f - kKnee;
    for (int i = 0; i < frames * 2; ++i) {
        float v = static_cast<float>(out[i]) + buf[static_cast<size_t>(i)] * gain;
        const float m = std::fabs(v);
        if (m > kKnee) v = std::copysign(kKnee + kRoom * std::tanh((m - kKnee) / kRoom), v);
        out[i] = static_cast<int16_t>(std::clamp(static_cast<int>(v), -32768, 32767));
    }
}

void SynthMusic::endFrame(uint64_t clock, std::vector<int16_t>& out, size_t from, double rate) {
    gm::Synth& s = *synth_;
    if (!s.font) {
        events_.clear();
        frameStart_ = clock;
        return;
    }
    if (s.rate == 0) {
        // The output rate moves a little every frame to keep the queue level; the synth
        // keeps the first one, which is near enough.
        s.rate = static_cast<int>(std::lround(rate));
        tsf_set_output(s.font, TSF_STEREO_INTERLEAVED, s.rate, 0.0f);
        tsf_set_max_voices(s.font, 64);
        for (int ch = 0; ch < 16; ++ch) {
            tsf_channel_set_pitchrange(s.font, ch, static_cast<float>(kBendRange));
            tsf_channel_set_presetnumber(s.font, ch, 0, ch == kDrums);
        }
        channels_ = {};
        s.effects.setRate(s.rate);
        s.equalizer.setRate(s.rate);
    }
    const int frames = static_cast<int>((out.size() - std::min(out.size(), from * 2)) / 2);
    const double span = clock > frameStart_ ? static_cast<double>(clock - frameStart_) : 1.0;
    int done = 0;
    for (const Event& e : events_) {
        const double t = e.clock > frameStart_ ? static_cast<double>(e.clock - frameStart_) / span : 0.0;
        const int at = std::clamp(static_cast<int>(t * frames), done, frames);
        render(out.data() + (from + static_cast<size_t>(done)) * 2, at - done);
        done = at;
        apply(e);
    }
    render(out.data() + (from + static_cast<size_t>(done)) * 2, frames - done);
    events_.clear();
    frameStart_ = clock;
}

}  // namespace plumnin
