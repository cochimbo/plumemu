#include "core/mp3_encode.h"

#include <algorithm>

extern "C" {
#include <layer3.h>
}

namespace plum {

namespace {

// An ID3v2.3 text frame, in UTF-16 with its byte order mark.
void textFrame(std::vector<uint8_t>& tag, const char* id, const std::string& text) {
    if (text.empty()) return;
    std::vector<uint8_t> body = {1, 0xFF, 0xFE};
    for (size_t i = 0; i < text.size();) {
        uint32_t c = static_cast<uint8_t>(text[i]);
        int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        c &= more == 3 ? 0x07 : more == 2 ? 0x0F : more == 1 ? 0x1F : 0x7F;
        ++i;
        for (; more > 0 && i < text.size(); --more, ++i) c = c << 6 | (static_cast<uint8_t>(text[i]) & 0x3F);
        auto unit = [&](uint32_t u) {
            body.push_back(static_cast<uint8_t>(u));
            body.push_back(static_cast<uint8_t>(u >> 8));
        };
        if (c >= 0x10000) {
            c -= 0x10000;
            unit(0xD800 + (c >> 10));
            unit(0xDC00 + (c & 0x3FF));
        } else {
            unit(c);
        }
    }
    tag.insert(tag.end(), id, id + 4);
    const uint32_t size = static_cast<uint32_t>(body.size());
    for (int s = 24; s >= 0; s -= 8) tag.push_back(static_cast<uint8_t>(size >> s));
    tag.push_back(0);
    tag.push_back(0);
    tag.insert(tag.end(), body.begin(), body.end());
}

std::vector<uint8_t> id3(const Mp3Tag& t) {
    std::vector<uint8_t> frames;
    textFrame(frames, "TIT2", t.title);
    textFrame(frames, "TALB", t.album);
    textFrame(frames, "TPE1", t.artist);
    if (t.track > 0) textFrame(frames, "TRCK", std::to_string(t.track));
    textFrame(frames, "TSSE", "plumsg");
    std::vector<uint8_t> out = {'I', 'D', '3', 3, 0, 0};
    const uint32_t size = static_cast<uint32_t>(frames.size());
    for (int s = 21; s >= 0; s -= 7) out.push_back(static_cast<uint8_t>((size >> s) & 0x7F));  // synchsafe
    out.insert(out.end(), frames.begin(), frames.end());
    return out;
}

}  // namespace

std::vector<uint8_t> encodeMp3(const std::vector<int16_t>& stereo, int rate, int kbps, const Mp3Tag& tag) {
    if (shine_check_config(rate, kbps) < 0) return {};
    shine_config_t config{};
    config.wave.channels = PCM_STEREO;
    config.wave.samplerate = rate;
    shine_set_config_mpeg_defaults(&config.mpeg);
    config.mpeg.mode = STEREO;
    config.mpeg.bitr = kbps;
    shine_t encoder = shine_initialise(&config);
    if (!encoder) return {};
    std::vector<uint8_t> out = id3(tag);
    const size_t pass = static_cast<size_t>(shine_samples_per_pass(encoder));
    std::vector<int16_t> block(pass * 2);
    const size_t frames = stereo.size() / 2;
    for (size_t at = 0; at < frames; at += pass) {
        const size_t n = std::min(pass, frames - at);
        std::copy(stereo.begin() + static_cast<long>(at * 2), stereo.begin() + static_cast<long>((at + n) * 2), block.begin());
        std::fill(block.begin() + static_cast<long>(n * 2), block.end(), static_cast<int16_t>(0));
        int written = 0;
        const unsigned char* data = shine_encode_buffer_interleaved(encoder, block.data(), &written);
        if (data && written > 0) out.insert(out.end(), data, data + written);
    }
    int written = 0;
    const unsigned char* data = shine_flush(encoder, &written);
    if (data && written > 0) out.insert(out.end(), data, data + written);
    shine_close(encoder);
    return out;
}

}  // namespace plum
