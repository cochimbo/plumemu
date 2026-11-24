#include "song_export.h"

#include <algorithm>
#include <cmath>

#include "vgm.h"

namespace plumsg {

double renderedSeconds(const VgmFile& file, const SongRender& how) {
    double seconds;
    if (file.loop && file.loopSamples > 0) {
        const double loop = static_cast<double>(file.loopSamples) / 44100.0;
        const double intro = std::max(0.0, file.seconds() - loop);
        seconds = intro + loop * std::max(1, how.loops) + how.fadeSeconds;
    } else {
        seconds = file.seconds() + 1.5;  // what still rings when it ends
    }
    return std::clamp(seconds, 1.0, how.maxSeconds);
}

bool renderSong(const VgmFile& file, RolandSound* roland, const RolandSound::Assignments* game,
                const RolandSound::Assignments* song, const SongRender& how,
                std::vector<int16_t>* out, std::atomic<float>* progress, const std::atomic<bool>* cancel) {
    const double total = renderedSeconds(file, how);
    const bool fades = (file.loop && file.loopSamples > 0) || total >= how.maxSeconds;
    VgmPlayer player;
    player.setRoland(roland);
    player.load(file);
    player.setLoop(true);
    if (roland) {
        // As the editor plays it: every instrument heard before the first note.
        roland->allNotesOff();
        roland->clearSeen();
        player.analyse();
        RolandSound::Assignments all = game ? *game : RolandSound::Assignments{};
        if (song)
            for (const auto& [fp, a] : *song) {
                // (Over what the game has under an old fingerprint of it, too.)
                for (uint32_t old : roland->aliases(fp)) all.erase(old);
                all[fp] = a;
            }
        roland->setAssignments(std::move(all));
    }
    player.setRolandOutput(roland != nullptr);
    const size_t frames = static_cast<size_t>(std::llround(total * how.rate));
    out->clear();
    out->reserve(frames * 2);
    constexpr double kStep = 1.0 / 60.0;
    while (out->size() / 2 < frames) {
        if (cancel && *cancel) {
            if (roland) roland->allNotesOff();
            return false;
        }
        if (player.finished()) {
            out->resize(out->size() + 2 * static_cast<size_t>(std::lround(kStep * how.rate)), 0);
        } else {
            if (roland) roland->waitForTimbres();  // the same sound on every export
            player.play(kStep, how.rate, *out);
        }
        if (progress)
            *progress = static_cast<float>(std::min(1.0, static_cast<double>(out->size() / 2) / static_cast<double>(frames)));
    }
    if (roland) roland->allNotesOff();
    out->resize(frames * 2);
    if (fades) {
        const size_t fade = std::min(frames, static_cast<size_t>(how.fadeSeconds * how.rate));
        for (size_t i = 0; i < fade; ++i) {
            const double g = 0.5 + 0.5 * std::cos(3.14159265358979 * static_cast<double>(i + 1) / static_cast<double>(fade));
            const size_t at = (frames - fade + i) * 2;
            for (size_t c = 0; c < 2; ++c) (*out)[at + c] = static_cast<int16_t>(std::lround((*out)[at + c] * g));
        }
    }
    return true;
}

}  // namespace plumsg
