// Sacar canciones on the Super Nintendo. Its music is played by a processor of its own,
// the SPC700, with a driver the game loads into it; the game asks it for a song through
// four ports, after loading the song's notes and samples if they are not there yet. So
// the songs are asked for as the game itself asks: the game is run for a while, watching
// what it writes to the ports and which byte of its memory each value came from; then,
// from just before the game wrote that byte, it is run again with another number written
// in its place, for every number. What starts other music on several voices is a song,
// and the sound processor as the song starts, saved as an SPC, plays it by itself.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "i18n.h"
#include "snes/snes.h"
#include "songs.h"

namespace plumnin {

namespace {

constexpr int kWindow = 360;     // frames a number is listened to, from where the game asked
constexpr size_t kRing = 120;    // frames back a byte may have been written before it is sent
constexpr int kMaxTests = 80;    // bytes tried, at most
constexpr int kMaxStarts = 6;    // moments a byte that asks for songs is tried from
constexpr int kDeadNumbers = 24;  // numbers in a row with no new song before giving up

// A key on, as the game is run frame by frame.
struct KeyOn {
    uint32_t frame;
    uint8_t voice, sample;
    uint16_t pitch;
};

// The voices keyed on since `seen`, added to `out`.
void keyOns(const snes::Apu& apu, std::array<uint32_t, 8>& seen, std::vector<KeyOn>* out, uint32_t frame) {
    const snes::Dsp& d = apu.dsp();
    for (int i = 0; i < 8; ++i) {
        const uint32_t k = d.keyOns(i);
        if (k == seen[static_cast<size_t>(i)]) continue;
        seen[static_cast<size_t>(i)] = k;
        if (out)
            out->push_back({frame, static_cast<uint8_t>(i), d.read(static_cast<uint8_t>(i * 16 + 4)),
                            static_cast<uint16_t>(d.read(static_cast<uint8_t>(i * 16 + 2)) |
                                                  (d.read(static_cast<uint8_t>(i * 16 + 3)) & 0x3F) << 8)});
    }
}

// What played in a stretch of frames.
struct Heard {
    int notes = 0, voices = 0, late = 0;
    std::array<std::vector<uint32_t>, 8> lines;  // each voice's first notes (sample and pitch)
    uint64_t signature = 0;
    // Music: notes on two voices or more, still going at the end.
    bool music() const { return notes >= 10 && voices >= 2 && late >= 3; }
    // On how many voices it is not what `o` was: a sound effect over the music changes one
    // or two, another song most.
    int differs(const Heard& o) const {
        int n = 0;
        for (size_t v = 0; v < 8; ++v) n += lines[v] != o.lines[v];
        return n;
    }
};

Heard judge(const std::vector<KeyOn>& keys, uint32_t from, uint32_t to) {
    Heard h;
    uint32_t voices = 0;
    for (const KeyOn& k : keys) {
        if (k.frame < from || k.frame >= to) continue;
        ++h.notes;
        voices |= 1u << k.voice;
        if (k.frame + 60 >= to) ++h.late;
        auto& line = h.lines[k.voice];
        if (line.size() < 8) line.push_back(k.sample * 0x10000u + (k.pitch >> 7));
    }
    uint64_t sig = 1469598103934665603ull;
    for (const auto& line : h.lines) {
        for (uint32_t x : line) sig = (sig ^ x) * 1099511628211ull;
        sig = (sig ^ 0xFFFF) * 1099511628211ull;
    }
    for (uint32_t v = voices; v; v &= v - 1) ++h.voices;
    h.signature = sig;
    return h;
}

// A byte of the work RAM the game sent to a port: where, the value, the frame it was
// written (and the console just before that frame) and the frame it was sent.
struct Asked {
    uint32_t index = 0;
    uint8_t value = 0;
    uint32_t written = 0, sent = 0;
    std::shared_ptr<const std::vector<uint8_t>> state;
};

class SnesRipper {
public:
    SnesRipper(const std::vector<uint8_t>& rom, const SongRipper::Options& o, SongRipper::Progress* p)
        : rom_(rom), options_(o), progress_(p) {}

    bool run(std::vector<SongRipper::Song>* songs, std::string* error, std::string* report);

private:
    bool cancelled() const { return progress_ && progress_->cancel.load(); }
    void step() {
        if (progress_) ++progress_->step;
    }
    void trace();
    // The game run again from `a`'s moment, the byte written as `number` (none if < 0), for
    // `extra` frames past where the game sent it; its key ons, and with `spcs` the sound
    // processor at the end of every frame.
    void listen(const Asked& a, int number, std::vector<KeyOn>* keys, std::vector<std::vector<uint8_t>>* spcs);
    // From the sound processor as the song starts: how long to play it (its intro and loop
    // twice, or to its end), and what it is wherever it was started. False if too short.
    bool measure(const std::vector<uint8_t>& spc, double* seconds, uint64_t* identity, std::string* log);

    const std::vector<uint8_t>& rom_;
    SongRipper::Options options_;
    SongRipper::Progress* progress_;
    std::unique_ptr<Snes> snes_;
    std::vector<Asked> asked_;
};

void SnesRipper::trace() {
    Snes& s = *snes_;
    struct Read {
        uint32_t index;
        uint8_t value;
    };
    std::deque<Read> reads;
    std::vector<uint32_t> lastWrite(0x20000, UINT32_MAX);
    std::deque<std::shared_ptr<const std::vector<uint8_t>>> ring;  // the console as each of the last frames began
    std::array<uint8_t, 4> last{};
    std::vector<Asked> pending;
    std::map<uint32_t, int> perIndex;
    int portWrites = 0;
    s.wramReadWatch = [&](uint32_t i, uint8_t v) {
        reads.push_back({i, v});
        if (reads.size() > 48) reads.pop_front();
    };
    s.wramWatch = [&](uint32_t i, uint8_t&) { lastWrite[i] = static_cast<uint32_t>(s.frames()); };
    s.apu().portWatch = [&](int p, uint8_t v) {
        ++portWrites;
        if (last[static_cast<size_t>(p)] == v) return;
        last[static_cast<size_t>(p)] = v;
        if (v == 0) return;
        // The bytes just read that held the value sent.
        std::set<uint32_t> seen;
        const uint32_t now = static_cast<uint32_t>(s.frames());
        for (auto it = reads.rbegin(); it != reads.rend(); ++it) {
            if (it->value != v || !seen.insert(it->index).second) continue;
            const uint32_t written = lastWrite[it->index];
            if (written == UINT32_MAX || now - written >= ring.size()) continue;
            pending.push_back({it->index, v, written, now, ring[ring.size() - 1 - (now - written)]});
        }
    };
    const int frames = static_cast<int>(std::max(options_.traceSeconds, 80.0) * 60);
    for (int f = 0; f < frames && !cancelled(); ++f) {
        auto state = std::make_shared<std::vector<uint8_t>>();
        s.saveState(*state, true);
        ring.push_back(std::move(state));
        if (ring.size() > kRing) ring.pop_front();
        // The title and the demo first; then Start now and then, into the game.
        PadState pad;
        pad.start = f > 1800 && f % 240 < 4;
        s.setPads(pad, PadState{});
        portWrites = 0;
        const size_t before = pending.size();
        s.runFrame();
        s.audio().clearSamples();
        // (Not what the game sends while it loads the driver or the music: lots a frame.)
        if (portWrites > 64) pending.resize(before);
        for (size_t i = before; i < pending.size(); ++i)
            if (perIndex[pending[i].index]++ < 4) asked_.push_back(pending[i]);
        pending.clear();
    }
    s.wramReadWatch = nullptr;
    s.wramWatch = nullptr;
    s.apu().portWatch = nullptr;
    if (asked_.size() > static_cast<size_t>(kMaxTests)) asked_.resize(kMaxTests);
}

void SnesRipper::listen(const Asked& a, int number, std::vector<KeyOn>* keys, std::vector<std::vector<uint8_t>>* spcs) {
    Snes& s = *snes_;
    s.loadState(*a.state, nullptr);
    bool done = number < 0;
    s.wramWatch = [&](uint32_t i, uint8_t& v) {
        if (!done && i == a.index && v == a.value) {
            v = static_cast<uint8_t>(number);
            done = true;
        }
    };
    std::array<uint32_t, 8> seen{};
    for (int i = 0; i < 8; ++i) seen[static_cast<size_t>(i)] = s.apu().dsp().keyOns(i);
    const uint32_t frames = a.sent - a.written + kWindow;
    for (uint32_t f = 0; f < frames && !cancelled(); ++f) {
        s.setPads(PadState{}, PadState{});
        s.runFrame();
        s.audio().clearSamples();
        keyOns(s.apu(), seen, keys, a.written + f);
        if (spcs) spcs->push_back(s.musicSnapshot({}, 0));
    }
    s.wramWatch = nullptr;
}

bool SnesRipper::measure(const std::vector<uint8_t>& spc, double* seconds, uint64_t* identity, std::string* log) {
    snes::Apu apu;
    apu.setMasterRate(1024000.0);  // a clock of the main processor's is one of the SPC700's
    if (!apu.loadSpc(spc)) return false;
    struct Note {
        double t;
        uint32_t token;  // voice, sample and pitch
    };
    std::vector<Note> notes;
    std::array<uint32_t, 8> seen{};
    for (int i = 0; i < 8; ++i) seen[static_cast<size_t>(i)] = apu.dsp().keyOns(i);
    std::vector<int16_t> sink;
    double loopStart = -1, loopLength = 0, end = -1, lastNote = 0;
    constexpr int kTick = 32 * 64;  // 2 ms of the sound processor
    const double longest = std::min(options_.maxSeconds, 240.0);
    for (int tick = 0; tick < static_cast<int>(longest * 500) && !cancelled(); ++tick) {
        apu.addTime(kTick);
        apu.catchUp();
        apu.takeSamples(sink);
        sink.clear();
        const double t = tick / 500.0;
        const snes::Dsp& d = apu.dsp();
        for (int i = 0; i < 8; ++i) {
            const uint32_t k = d.keyOns(i);
            if (k == seen[static_cast<size_t>(i)]) continue;
            seen[static_cast<size_t>(i)] = k;
            const uint32_t pitch = (d.read(static_cast<uint8_t>(i * 16 + 2)) |
                                    (d.read(static_cast<uint8_t>(i * 16 + 3)) & 0x3F) << 8) >> 6;
            notes.push_back({t, static_cast<uint32_t>(i) << 24 | static_cast<uint32_t>(d.read(static_cast<uint8_t>(i * 16 + 4))) << 16 | pitch});
            lastNote = t;
        }
        // Silent for long: it ended.
        if (!notes.empty() && t - lastNote > 8) {
            end = lastNote + 2;
            break;
        }
        // Every ten seconds past the first forty: does it go round? A turn is a number of
        // notes the last ones repeat, over the last twenty seconds at least (a riff
        // repeated is no loop yet).
        if (tick % 5000 != 4999 || t < 40) continue;
        const size_t n = notes.size();
        for (size_t p = 8; p * 2 <= n; ++p) {
            size_t recent = n;
            while (recent > 0 && notes[recent - 1].t >= notes[n - 1].t - 20) --recent;
            const size_t span = std::max<size_t>({p, 48, n - recent});
            if (span + p > n) break;
            bool same = true;
            for (size_t i = n - span; i < n && same; ++i) same = notes[i].token == notes[i - p].token;
            if (!same) continue;
            size_t first = n - span;
            while (first > p && notes[first - 1].token == notes[first - 1 - p].token) --first;
            loopStart = notes[first - p].t;
            loopLength = notes[n - 1].t - notes[n - 1 - p].t;
            break;
        }
        if (loopLength > 1) break;
    }
    if (notes.size() < 16) return false;
    // A short loop goes round for a minute at least.
    *seconds = loopLength > 1 ? loopStart + loopLength * std::max(2.0, std::ceil(60 / loopLength))
               : end > 0      ? end
                              : longest;
    // What it is, wherever it was started: the notes (not their voices) of a turn of its
    // loop, in any order, or of all of it.
    std::vector<uint32_t> body;
    for (const Note& x : notes)
        if (loopLength <= 1 || x.t >= notes.back().t - loopLength + 0.001) body.push_back(x.token & 0xFFFFFF);
    std::sort(body.begin(), body.end());
    uint64_t h = 1469598103934665603ull;
    for (uint32_t x : body) h = (h ^ x) * 1099511628211ull;
    *identity = h;
    if (log) {
        char line[128];
        std::snprintf(line, sizeof line, "%zu notes, intro %.1f s, loop %.1f s, end %.1f s", notes.size(), loopStart,
                      loopLength, end);
        *log = line;
    }
    return true;
}

bool SnesRipper::run(std::vector<SongRipper::Song>* songs, std::string* error, std::string* report) {
    const bool debug = std::getenv("PLUMNIN_RIP_DEBUG") != nullptr;
    std::string log;
    snes_ = std::make_unique<Snes>();
    if (!snes_->loadRomData(rom_, error)) return false;
    snes_->setSampleRate(44100);
    snes_->setDrawing(false);
    if (progress_) progress_->steps = 3 + kMaxTests;

    // 1. The game for a while: what it sends the sound processor, and from where.
    trace();
    step();
    if (cancelled()) return false;

    // 2. The bytes that, written otherwise, start other music.
    std::set<uint32_t> ways;
    for (const Asked& a : asked_) {
        if (cancelled()) return false;
        step();
        if (ways.count(a.index)) continue;
        std::vector<KeyOn> k0, k1;
        listen(a, -1, &k0, nullptr);
        const Heard before = judge(k0, a.sent, a.sent + kWindow);
        const int other = a.value > 1 ? a.value - 1 : a.value + 1;
        listen(a, other, &k1, nullptr);
        const Heard after = judge(k1, a.sent, a.sent + kWindow);
        const bool way = after.music() && (!before.music() || after.differs(before) >= 3);
        if (debug)
            std::fprintf(stderr, "%05X = %02X (written %u, sent %u): %d notes, music %d | %02X: %d notes, music %d%s\n",
                         a.index, a.value, a.written, a.sent, before.notes, before.music(), other, after.notes,
                         after.music(), way ? ", asks for songs" : "");
        if (way) ways.insert(a.index);
    }
    if (ways.empty()) {
        if (error) *error = tr("No se ha encontrado cómo pide el juego sus canciones.");
        return false;
    }

    // 3. Every number, from each moment the game asked that way.
    struct Found {
        const Asked* from;
        int number;
    };
    std::vector<Found> found;
    std::set<uint64_t> signatures;
    std::vector<const Asked*> starts;
    std::map<uint32_t, int> perWay;
    for (const Asked& a : asked_)
        if (ways.count(a.index) && perWay[a.index]++ < kMaxStarts) starts.push_back(&a);
    if (progress_) progress_->steps = progress_->step + static_cast<int>(starts.size()) * 2 + 8;
    for (const Asked* a : starts) {
        step();
        std::vector<KeyOn> k0;
        listen(*a, -1, &k0, nullptr);
        const Heard before = judge(k0, a->sent, a->sent + kWindow);
        if (before.music() && signatures.insert(before.signature).second) found.push_back({a, a->value});
        int dead = 0;
        for (int n = 0; n < 256 && dead < kDeadNumbers && !cancelled(); ++n) {
            std::vector<KeyOn> k;
            listen(*a, n, &k, nullptr);
            const Heard h = judge(k, a->sent, a->sent + kWindow);
            if (!h.music() || (before.music() && h.differs(before) < 3)) {
                if (!found.empty()) ++dead;
                continue;
            }
            dead = 0;
            if (!signatures.insert(h.signature).second) continue;
            found.push_back({a, n});
            if (progress_) ++progress_->found;
            if (debug) std::fprintf(stderr, "  %05X = %d: %d notes on %d voices\n", a->index, n, h.notes, h.voices);
        }
    }
    if (progress_) progress_->steps = progress_->step + static_cast<int>(found.size()) + 1;

    // 4. Each one as an SPC from the moment it starts, its length that of its loop.
    std::set<uint64_t> identities;
    if (progress_) progress_->found = 0;
    for (const Found& f : found) {
        if (cancelled()) return false;
        step();
        std::vector<KeyOn> keys;
        std::vector<std::vector<uint8_t>> spcs;
        listen(*f.from, f.number, &keys, &spcs);
        long onset = -1;
        for (const KeyOn& k : keys)
            if (k.frame >= f.from->sent) {
                onset = static_cast<long>(k.frame);
                break;
            }
        const long at = onset - static_cast<long>(f.from->written) - 1;  // the frame before
        if (onset < 0 || at < 0 || at >= static_cast<long>(spcs.size())) continue;
        const std::vector<uint8_t>& spc = spcs[static_cast<size_t>(at)];
        double seconds = 0;
        uint64_t identity = 0;
        std::string how;
        if (!measure(spc, &seconds, &identity, &how)) continue;
        if (!identities.insert(identity).second) {
            if (debug) std::fprintf(stderr, "  %05X = %d: heard before\n", f.from->index, f.number);
            continue;
        }
        snes::Apu apu;
        apu.setMasterRate(1024000.0);
        apu.loadSpc(spc);
        SongRipper::Song song;
        song.number = f.number;
        song.spc = true;
        song.vgz = apu.saveSpc(trf("Canción %02d", static_cast<int>(songs->size()) + 1), snes_->title(),
                               static_cast<int>(std::lround(seconds)));
        song.seconds = seconds;
        songs->push_back(std::move(song));
        if (progress_) ++progress_->found;
        char line[160];
        std::snprintf(line, sizeof line, "Canción %02d: número %d en %05X, %s\n", static_cast<int>(songs->size()), f.number,
                      f.from->index, how.c_str());
        log += line;
        if (debug) std::fprintf(stderr, "%s", line);
    }
    if (report) *report = log;
    if (songs->empty() && error) *error = tr("No se ha encontrado cómo pide el juego sus canciones.");
    return !songs->empty();
}

}  // namespace

bool ripSnesSongs(const std::vector<uint8_t>& rom, const SongRipper::Options& options, SongRipper::Progress* progress,
                  std::vector<SongRipper::Song>* songs, std::string* error, std::string* report) {
    SnesRipper r(rom, options, progress);
    return r.run(songs, error, report);
}

}  // namespace plumnin
