#include "core/vgm_format.h"

#include <algorithm>
#include <cstring>

#include "core/i18n.h"
#include "miniz.h"

namespace plum {
namespace vgm {

uint32_t get32(const std::vector<uint8_t>& d, size_t at) {
    if (at + 4 > d.size()) return 0;
    return static_cast<uint32_t>(d[at]) | static_cast<uint32_t>(d[at + 1]) << 8 | static_cast<uint32_t>(d[at + 2]) << 16 |
           static_cast<uint32_t>(d[at + 3]) << 24;
}

void put32(std::vector<uint8_t>& d, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) d[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}

void append32(std::vector<uint8_t>& d, uint32_t v) {
    for (int i = 0; i < 4; ++i) d.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void appendUtf16(std::vector<uint8_t>& d, const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        uint32_t c = static_cast<uint8_t>(s[i]);
        int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        c &= more == 3 ? 0x07 : more == 2 ? 0x0F : more == 1 ? 0x1F : 0x7F;
        ++i;
        for (; more > 0 && i < s.size(); --more, ++i) c = c << 6 | (static_cast<uint8_t>(s[i]) & 0x3F);
        if (c >= 0x10000) {
            c -= 0x10000;
            const uint32_t hi = 0xD800 + (c >> 10), lo = 0xDC00 + (c & 0x3FF);
            d.push_back(static_cast<uint8_t>(hi));
            d.push_back(static_cast<uint8_t>(hi >> 8));
            d.push_back(static_cast<uint8_t>(lo));
            d.push_back(static_cast<uint8_t>(lo >> 8));
        } else {
            d.push_back(static_cast<uint8_t>(c));
            d.push_back(static_cast<uint8_t>(c >> 8));
        }
    }
    d.push_back(0);
    d.push_back(0);
}

// Reads one UTF-16 string at `at`, moving past its terminator.
std::string readUtf16(const std::vector<uint8_t>& d, size_t& at, size_t end) {
    std::string out;
    while (at + 1 < end) {
        uint32_t c = d[at] | d[at + 1] << 8;
        at += 2;
        if (c == 0) break;
        if (c >= 0xD800 && c < 0xDC00 && at + 1 < end) {
            const uint32_t lo = d[at] | d[at + 1] << 8;
            at += 2;
            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | c >> 6);
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | c >> 12);
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | c >> 18);
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::vector<uint8_t> gzip(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 0xFF};
    size_t size = 0;
    void* deflated = tdefl_compress_mem_to_heap(in.data(), in.size(), &size, TDEFL_DEFAULT_MAX_PROBES);
    if (!deflated) return in;
    const uint8_t* p = static_cast<const uint8_t*>(deflated);
    out.insert(out.end(), p, p + size);
    mz_free(deflated);
    append32(out, static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, in.data(), in.size())));
    append32(out, static_cast<uint32_t>(in.size()));
    return out;
}

bool gunzip(const std::vector<uint8_t>& in, std::vector<uint8_t>* out) {
    if (in.size() < 18 || in[0] != 0x1F || in[1] != 0x8B || in[2] != 8) return false;
    const uint8_t flags = in[3];
    size_t at = 10;
    if (flags & 4) at += 2 + static_cast<size_t>(in[at] | in[at + 1] << 8);  // extra field
    for (int f : {8, 16})                                                     // name, comment
        if (flags & f) {
            while (at < in.size() && in[at]) ++at;
            ++at;
        }
    if (flags & 2) at += 2;  // header CRC
    if (at >= in.size()) return false;
    size_t size = 0;
    void* p = tinfl_decompress_mem_to_heap(in.data() + at, in.size() - at, &size, 0);
    if (!p) return false;
    const uint8_t* b = static_cast<const uint8_t*>(p);
    out->assign(b, b + size);
    mz_free(p);
    return true;
}


void appendWait(std::vector<uint8_t>& data, uint64_t* samples, uint64_t target, bool frameWaits) {
    while (target > *samples) {
        const uint64_t n = std::min<uint64_t>(target - *samples, 65535);
        if (n <= 16) {
            data.push_back(static_cast<uint8_t>(0x70 + n - 1));
        } else if (frameWaits && (n == 735 || n == 882)) {
            data.push_back(n == 735 ? 0x62 : 0x63);
        } else {
            data.insert(data.end(), {0x61, static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8)});
        }
        *samples += n;
    }
}

void appendTag(std::vector<uint8_t>& file, const std::vector<std::string>& fields) {
    put32(file, 0x14, static_cast<uint32_t>(file.size() - 0x14));
    std::vector<uint8_t> gd3;
    for (const std::string& s : fields) appendUtf16(gd3, s);
    file.insert(file.end(), {'G', 'd', '3', ' '});
    append32(file, 0x100);
    append32(file, static_cast<uint32_t>(gd3.size()));
    file.insert(file.end(), gd3.begin(), gd3.end());
    put32(file, 0x04, static_cast<uint32_t>(file.size() - 4));
}

bool isVgm(const std::vector<uint8_t>& d, uint32_t* version) {
    if (d.size() < 0x40 || std::memcmp(d.data(), "Vgm ", 4) != 0) return false;
    *version = get32(d, 0x08);
    return true;
}

bool readLayout(const std::vector<uint8_t>& d, Layout* out, std::string* error) {
    out->version = get32(d, 0x08);
    out->start = out->version >= 0x150 && get32(d, 0x34) ? 0x34 + get32(d, 0x34) : 0x40;
    if (out->start >= d.size()) {
        if (error) *error = tr("El VGM está cortado.");
        return false;
    }
    out->samples = get32(d, 0x18);
    out->loop = get32(d, 0x1C) ? 0x1C + get32(d, 0x1C) : 0;
    out->loopSamples = out->loop ? get32(d, 0x20) : 0;
    if (out->loop >= d.size() || out->loopSamples > out->samples) out->loop = 0, out->loopSamples = 0;
    out->title.clear();
    out->game.clear();
    if (const uint32_t g = get32(d, 0x14); g && 0x14 + g + 12 <= d.size() && std::memcmp(&d[0x14 + g], "Gd3 ", 4) == 0) {
        size_t at = 0x14 + g + 12;
        const size_t end = std::min(d.size(), at + get32(d, 0x14 + g + 8));
        out->title = readUtf16(d, at, end);
        readUtf16(d, at, end);
        out->game = readUtf16(d, at, end);
    }
    return true;
}

}  // namespace vgm
}  // namespace plum
