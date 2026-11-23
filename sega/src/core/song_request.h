#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace plumsg {

// How a game asks its sound driver for a song, as Sacar canciones found it (SongExtractor):
// what to watch while the game plays to know exactly which song it has started, even two
// that share all their instruments.
//
// - Byte: the game writes the song's number to a byte of RAM (the driver's queue). The
//   address as MegaDrive::setMemoryWatch gives it: work RAM at 0xFF0000, the Z80's at
//   0xA00000, a Master System's at its own (0xC000 up).
// - Call: the game calls a routine of the driver, at `address`, with the number in a
//   register (d0-d7 as 0-7, a0-a6 as 8-14) or, with `numberReg` -1, on the stack
//   (`argBytes` 2 or 4, right above the return address).
// - Ring: GEMS's. The game writes `message` and then the number after the last message,
//   in the ring at `ringStart` (its size `ringMask` + 1), and then where the next goes to
//   the byte at `address`.
struct SongRequest {
    enum class Way : uint8_t { Byte, Call, Ring };
    Way way = Way::Byte;
    uint32_t address = 0;
    int numberReg = 0;
    int argBytes = 0;
    uint32_t ringStart = 0, ringMask = 0;
    std::vector<uint8_t> message;

    bool operator==(const SongRequest& o) const {
        return way == o.way && address == o.address && numberReg == o.numberReg && argBytes == o.argBytes &&
               ringStart == o.ringStart && ringMask == o.ringMask && message == o.message;
    }
    bool operator!=(const SongRequest& o) const { return !(*this == o); }

    // As it goes in a song's file, with the song's number after it: "byte ff0c09",
    // "rutina 0072a4 d0", "rutina 0072a4 pila2", "anillo a01c00 a01b00 3f ff10".
    std::string format() const {
        char buf[64];
        switch (way) {
            case Way::Byte: std::snprintf(buf, sizeof buf, "byte %06x", static_cast<unsigned>(address)); return buf;
            case Way::Call:
                if (numberReg < 0)
                    std::snprintf(buf, sizeof buf, "rutina %06x pila%d", static_cast<unsigned>(address), argBytes);
                else
                    std::snprintf(buf, sizeof buf, "rutina %06x %c%d", static_cast<unsigned>(address),
                                  numberReg < 8 ? 'd' : 'a', numberReg % 8);
                return buf;
            case Way::Ring: {
                std::snprintf(buf, sizeof buf, "anillo %06x %06x %x ", static_cast<unsigned>(address),
                              static_cast<unsigned>(ringStart), static_cast<unsigned>(ringMask));
                std::string s = buf;
                for (uint8_t b : message) {
                    std::snprintf(buf, sizeof buf, "%02x", b);
                    s += buf;
                }
                if (message.empty()) s += "-";
                return s;
            }
        }
        return {};
    }
    // The other way round, from a song file's line "peticion = <format()> <number hex>";
    // false if it makes no sense.
    static bool parse(const std::string& text, SongRequest* out, uint32_t* number) {
        char kind[16] = "", a[16] = "", b[16] = "", c[16] = "", d[64] = "", e[16] = "";
        const int n = std::sscanf(text.c_str(), " %15s %15s %15s %15s %63s %15s", kind, a, b, c, d, e);
        SongRequest r;
        auto hex = [](const char* s, uint32_t* v) {
            char* stop = nullptr;
            const unsigned long x = std::strtoul(s, &stop, 16);
            if (stop == s || *stop) return false;
            *v = static_cast<uint32_t>(x);
            return true;
        };
        const std::string k = kind;
        const char* num = nullptr;
        if (k == "byte" && n >= 3) {
            r.way = Way::Byte;
            if (!hex(a, &r.address)) return false;
            num = b;
        } else if (k == "rutina" && n >= 4) {
            r.way = Way::Call;
            if (!hex(a, &r.address)) return false;
            const std::string where = b;
            if (where.compare(0, 4, "pila") == 0) {
                r.numberReg = -1;
                r.argBytes = std::atoi(where.c_str() + 4);
                if (r.argBytes != 2 && r.argBytes != 4) return false;
            } else if (where.size() == 2 && (where[0] == 'd' || where[0] == 'a') && where[1] >= '0' && where[1] <= '7') {
                r.numberReg = (where[0] == 'a' ? 8 : 0) + (where[1] - '0');
                if (r.numberReg > 14) return false;
            } else {
                return false;
            }
            num = c;
        } else if (k == "anillo" && n >= 6) {
            r.way = Way::Ring;
            if (!hex(a, &r.address) || !hex(b, &r.ringStart) || !hex(c, &r.ringMask)) return false;
            const std::string m = d;
            if (m != "-") {
                if (m.size() % 2) return false;
                for (size_t i = 0; i < m.size(); i += 2) {
                    uint32_t byte = 0;
                    if (!hex(m.substr(i, 2).c_str(), &byte)) return false;
                    r.message.push_back(static_cast<uint8_t>(byte));
                }
            }
            num = e;
        } else {
            return false;
        }
        uint32_t v = 0;
        if (!hex(num, &v)) return false;
        if (out) *out = std::move(r);
        if (number) *number = v;
        return true;
    }
};

}  // namespace plumsg
