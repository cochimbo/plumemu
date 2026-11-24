#include "headless/headless_tools.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

#include "miniz.h"

namespace plum {
namespace headless {

namespace {

uint32_t fnvAdd(uint32_t h, uint32_t v) {
    h ^= v;
    return h * 16777619u;
}

}  // namespace

// The same hash the tests and rom_runner print: FNV-1a over the visible pixels.
uint32_t frameHash(const VideoFrame& f) {
    uint32_t h = 2166136261u;
    for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x) h = fnvAdd(h, f.pixel(x, y));
    return h;
}

uint32_t soundHash(uint32_t h, const std::vector<int16_t>& samples) {
    for (int16_t s : samples) h = fnvAdd(h, static_cast<uint16_t>(s));
    return h;
}

bool writeFile(const std::string& path, const void* data, size_t size) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
}

bool readFile(const std::string& path, std::vector<uint8_t>& data) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool writePng(const std::string& path, const uint32_t* argb, int width, int height, int stride) {
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const uint32_t p = argb[y * stride + x];
            uint8_t* o = &rgba[(static_cast<size_t>(y) * width + x) * 4];
            o[0] = static_cast<uint8_t>(p >> 16);
            o[1] = static_cast<uint8_t>(p >> 8);
            o[2] = static_cast<uint8_t>(p);
            o[3] = 0xFF;
        }
    size_t size = 0;
    void* png = tdefl_write_image_to_png_file_in_memory(rgba.data(), width, height, 4, &size);
    if (!png) return false;
    const bool ok = writeFile(path, png, size);
    mz_free(png);
    return ok;
}

bool writeWav(const std::string& path, const std::vector<int16_t>& samples, int rate) {
    std::ofstream out(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = static_cast<uint32_t>(samples.size() * 2);
    out.write("RIFF", 4);
    u32(36 + bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(static_cast<uint32_t>(rate));
    u32(static_cast<uint32_t>(rate) * 4);
    u16(4);
    u16(16);
    out.write("data", 4);
    u32(bytes);
    out.write(reinterpret_cast<const char*>(samples.data()), bytes);
    return static_cast<bool>(out);
}

std::string hex8(uint32_t v) {
    char s[16];
    std::snprintf(s, sizeof s, "%08x", v);
    return s;
}

std::string jsonString(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if (static_cast<unsigned char>(c) < 0x20) {
            char u[8];
            std::snprintf(u, sizeof u, "\\u%04x", c);
            o += u;
        } else o += c;
    }
    return o + "\"";
}


bool parseFrames(const std::string& s, long* first, long* last) {
    char* end = nullptr;
    const long a = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || a < 0) return false;
    if (*end == 0) {
        *first = *last = a;
        return true;
    }
    const char sep = *end;
    const char* rest = end + 1;
    const long b = std::strtol(rest, &end, 10);
    if (end == rest || *end != 0) return false;
    if (sep == '-' && b >= a) *first = a, *last = b;
    else if (sep == '+' && b > 0) *first = a, *last = a + b - 1;
    else return false;
    return true;
}

namespace {

// A button of the pad by the name scripts give it: only the program's own (padButtons()).
bool setButton(PadState& p, const std::string& name) {
    static const std::map<std::string, bool PadState::*> kNames = {
        {"up", &PadState::up}, {"down", &PadState::down}, {"left", &PadState::left}, {"right", &PadState::right},
        {"a", &PadState::a},   {"b", &PadState::b},       {"c", &PadState::c},       {"x", &PadState::x},
        {"y", &PadState::y},   {"z", &PadState::z},       {"l", &PadState::l},       {"r", &PadState::r},
        {"start", &PadState::start}, {"select", &PadState::select}, {"mode", &PadState::mode},
    };
    const auto it = kNames.find(name);
    if (it == kNames.end()) return false;
    const PadButtons& own = padButtons();
    if (std::find(own.begin(), own.end(), it->second) == own.end()) return false;
    p.*(it->second) = true;
    return true;
}

}  // namespace

// Parses one script line into `out` (nothing for blank and comment lines).
bool parseLine(const std::string& raw, int lineNo, const std::string& where, std::vector<Command>& out) {
    std::string text = raw.substr(0, raw.find('#'));
    std::istringstream in(text);
    std::vector<std::string> words;
    for (std::string w; in >> w;) words.push_back(w);
    if (words.empty()) return true;
    auto fail = [&](const std::string& why) {
        std::fprintf(stderr, "%s:%d: %s\n", where.c_str(), lineNo, why.c_str());
        return false;
    };
    Command c;
    c.line = lineNo;
    if (!parseFrames(words[0], &c.first, &c.last)) return fail("bad frames '" + words[0] + "' (N, N-M or N+K)");
    if (words.size() < 2) return fail("nothing to do");
    std::string verb = words[1];
    for (char& ch : verb) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    static const std::map<std::string, Command::Kind> kVerbs = {
        {"screenshot", Command::Screenshot}, {"savestate", Command::SaveState}, {"loadstate", Command::LoadState},
        {"dump-ram", Command::DumpRam},      {"hash", Command::Hash},           {"reset", Command::Reset},
        {"power", Command::Power},           {"quit", Command::Quit},
    };
    const auto v = kVerbs.find(verb);
    if (v != kVerbs.end()) {
        c.kind = v->second;
        const bool needsFile = c.kind == Command::SaveState || c.kind == Command::LoadState || c.kind == Command::DumpRam;
        const bool takesFile = needsFile || c.kind == Command::Screenshot;
        if (words.size() > (takesFile ? 3u : 2u)) return fail("too many words for " + verb);
        if (needsFile && words.size() < 3) return fail(verb + " needs a file");
        if (words.size() == 3) c.file = words[2];
        out.push_back(c);
        return true;
    }
    for (size_t i = 1; i < words.size(); ++i) {
        std::string b = words[i];
        for (char& ch : b) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        int port = 0;
        if (b.rfind("p1.", 0) == 0) b = b.substr(3);
        else if (b.rfind("p2.", 0) == 0) b = b.substr(3), port = 1;
        if (!setButton(c.pad[port], b)) return fail("unknown button or command '" + words[i] + "'");
    }
    out.push_back(c);
    return true;
}

void orPad(PadState& d, const PadState& s) {
    for (bool PadState::*button : padButtons()) d.*button |= s.*button;
}



bool readLog(const std::string& path, std::vector<LogLine>& log) {
    std::ifstream in(path);
    if (!in) return false;
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        LogLine l{};
        unsigned long p = 0, s = 0;
        if (std::sscanf(line.c_str(), "%ld %lx %lx", &l.frame, &p, &s) < 2) continue;
        l.picture = static_cast<uint32_t>(p);
        l.sound = static_cast<uint32_t>(s);
        log.push_back(l);
    }
    return true;
}

}  // namespace headless
}  // namespace plum
