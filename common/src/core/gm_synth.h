#pragma once

// What both programs' General MIDI synthesizers play through (core/gm_sound.h): TinySoundFont
// with a reverb, a chorus and an equalizer after it. Only for the synthesizers' own code.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "core/gm_sound.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define TSF_NO_STDIO
#include "tsf.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace plum {
namespace gm {

// A Schroeder reverb (four combs and two all-pass filters per side, Freeverb's
// tunings) and a chorus (one delay line swept by a slow sine), like the SC-55's
// own effects. Both are sends: a part of the dry sound goes in, the result is added.
class Effects {
public:
    void setRate(double rate) {
        if (rate == rate_) return;
        rate_ = rate;
        const double scale = rate / 44100.0;
        static constexpr int kCombs[4] = {1116, 1188, 1277, 1356};
        static constexpr int kAllPasses[2] = {556, 441};
        for (int side = 0; side < 2; ++side) {
            const int spread = side ? 23 : 0;
            for (int i = 0; i < 4; ++i) combs_[side][i].assign(static_cast<size_t>((kCombs[i] + spread) * scale), 0.0f);
            for (int i = 0; i < 2; ++i) allPasses_[side][i].assign(static_cast<size_t>((kAllPasses[i] + spread) * scale), 0.0f);
        }
        combPos_ = {};
        allPassPos_ = {};
        combFilter_ = {};
        chorus_.assign(static_cast<size_t>(rate * 0.05) * 2, 0.0f);
        chorusPos_ = 0;
        phase_ = 0;
    }
    void setAmounts(int reverb, int chorus) {
        reverb_ = std::clamp(reverb, 0, 100) / 100.0f;
        chorusAmount_ = std::clamp(chorus, 0, 100) / 100.0f;
    }

    void process(float* buf, int frames) {
        if (rate_ <= 0) return;
        const size_t chorusLen = chorus_.size() / 2;
        for (int i = 0; i < frames; ++i) {
            float& l = buf[i * 2];
            float& r = buf[i * 2 + 1];
            // Chorus: a copy 10-20 ms late, its delay swaying at 0.6 Hz.
            if (chorusAmount_ > 0 && chorusLen > 0) {
                chorus_[chorusPos_ * 2] = l;
                chorus_[chorusPos_ * 2 + 1] = r;
                const double delay = rate_ * (0.015 + 0.005 * std::sin(phase_));
                phase_ += 2 * 3.14159265358979 * 0.6 / rate_;
                if (phase_ > 2 * 3.14159265358979) phase_ -= 2 * 3.14159265358979;
                double at = static_cast<double>(chorusPos_) - delay;
                while (at < 0) at += static_cast<double>(chorusLen);
                const size_t a = static_cast<size_t>(at) % chorusLen, b = (a + 1) % chorusLen;
                const float f = static_cast<float>(at - std::floor(at));
                const float cl = chorus_[a * 2] + (chorus_[b * 2] - chorus_[a * 2]) * f;
                const float cr = chorus_[a * 2 + 1] + (chorus_[b * 2 + 1] - chorus_[a * 2 + 1]) * f;
                chorusPos_ = (chorusPos_ + 1) % chorusLen;
                l += cr * chorusAmount_ * 0.5f;  // crossed, for width
                r += cl * chorusAmount_ * 0.5f;
            }
            if (reverb_ <= 0) continue;
            const float in = (l + r) * 0.015f * reverb_;
            float out[2] = {0, 0};
            for (int side = 0; side < 2; ++side) {
                for (int c = 0; c < 4; ++c) {
                    std::vector<float>& line = combs_[side][c];
                    size_t& pos = combPos_[side][c];
                    const float y = line[pos];
                    float& filter = combFilter_[side][c];
                    filter = y * 0.8f + filter * 0.2f;  // damping
                    line[pos] = in + filter * 0.84f;     // room size
                    pos = (pos + 1) % line.size();
                    out[side] += y;
                }
                for (int a = 0; a < 2; ++a) {
                    std::vector<float>& line = allPasses_[side][a];
                    size_t& pos = allPassPos_[side][a];
                    const float b = line[pos];
                    line[pos] = out[side] + b * 0.5f;
                    out[side] = b - out[side];
                    pos = (pos + 1) % line.size();
                }
            }
            l += out[0] * 3.0f;
            r += out[1] * 3.0f;
        }
    }

private:
    double rate_ = 0;
    float reverb_ = 0.4f, chorusAmount_ = 0.2f;
    std::array<std::array<std::vector<float>, 4>, 2> combs_;
    std::array<std::array<std::vector<float>, 2>, 2> allPasses_;
    std::array<std::array<size_t, 4>, 2> combPos_{};
    std::array<std::array<size_t, 2>, 2> allPassPos_{};
    std::array<std::array<float, 4>, 2> combFilter_{};
    std::vector<float> chorus_;
    size_t chorusPos_ = 0;
    double phase_ = 0;
};

// The equalizer: a low shelf, three peaks and a high shelf (RBJ's biquads), one gain
// each in dB. A band at 0 dB is left out.
class Equalizer {
public:
    void setRate(double rate) {
        rate_ = rate;
        design();
    }
    void setGains(const std::array<int, kEqBands>& gains) {
        if (gains == gains_) return;
        gains_ = gains;
        design();
    }

    void process(float* buf, int frames) {
        for (int b = 0; b < kEqBands; ++b) {
            if (!gains_[static_cast<size_t>(b)] || rate_ <= 0) continue;
            Band& f = bands_[static_cast<size_t>(b)];
            for (int i = 0; i < frames; ++i)
                for (int side = 0; side < 2; ++side) {
                    float& x = buf[i * 2 + side];
                    float* z = f.z[side];
                    const float y = f.b0 * x + z[0];
                    z[0] = f.b1 * x - f.a1 * y + z[1];
                    z[1] = f.b2 * x - f.a2 * y;
                    x = y;
                }
        }
    }

private:
    struct Band {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z[2][2] = {};
    };
    void design() {
        if (rate_ <= 0) return;
        for (int b = 0; b < kEqBands; ++b) {
            Band& f = bands_[static_cast<size_t>(b)];
            const double gain = std::pow(10.0, gains_[static_cast<size_t>(b)] / 40.0);
            const double w = 2 * 3.14159265358979 * std::min(kEqFrequencies[b], rate_ * 0.45) / rate_;
            const double cw = std::cos(w), sw = std::sin(w);
            double b0, b1, b2, a0, a1, a2;
            if (b == 0 || b == kEqBands - 1) {  // shelves, slope 1
                const double alpha = sw / 2 * std::sqrt(2.0), sq = 2 * std::sqrt(gain) * alpha;
                const double sign = b == 0 ? 1 : -1;  // the high shelf is the low one with cos negated
                b0 = gain * ((gain + 1) - sign * (gain - 1) * cw + sq);
                b1 = 2 * sign * gain * ((gain - 1) - sign * (gain + 1) * cw);
                b2 = gain * ((gain + 1) - sign * (gain - 1) * cw - sq);
                a0 = (gain + 1) + sign * (gain - 1) * cw + sq;
                a1 = -2 * sign * ((gain - 1) + sign * (gain + 1) * cw);
                a2 = (gain + 1) + sign * (gain - 1) * cw - sq;
            } else {  // peaks, Q 1: about an octave and a half wide
                const double alpha = sw / 2;
                b0 = 1 + alpha * gain;
                b1 = -2 * cw;
                b2 = 1 - alpha * gain;
                a0 = 1 + alpha / gain;
                a1 = -2 * cw;
                a2 = 1 - alpha / gain;
            }
            f.b0 = static_cast<float>(b0 / a0);
            f.b1 = static_cast<float>(b1 / a0);
            f.b2 = static_cast<float>(b2 / a0);
            f.a1 = static_cast<float>(a1 / a0);
            f.a2 = static_cast<float>(a2 / a0);
        }
    }

    double rate_ = 0;
    std::array<int, kEqBands> gains_{};
    std::array<Band, kEqBands> bands_{};
};

// The SoundFont synthesizer and what its sound goes through.
// Cuts short at once every voice still sounding `key` on `channel`: TinySoundFont keeps its
// voices private, so this lives beside its implementation.
void endKeyQuick(tsf* font, int channel, int key);

struct Synth {
    tsf* font = nullptr;
    int rate = 0;  // the rate tsf renders at, fixed once set
    Effects effects;
    Equalizer equalizer;
    std::vector<float> buffer;

    ~Synth() {
        if (font) tsf_close(font);
    }
};

}  // namespace gm
}  // namespace plum
