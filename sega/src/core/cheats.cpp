#include "cheats.h"

#include "core/i18n.h"

#include <cctype>
#include <cstring>

namespace plumsg {

namespace {

// The Genesis Game Genie's alphabet: each letter is 5 bits (no I, O, Q or U).
constexpr const char kGenie[] = "ABCDEFGHJKLMNPRSTVWXYZ0123456789";

int genieValue(char c) {
    const char* p = std::strchr(kGenie, c);
    return c && p ? static_cast<int>(p - kGenie) : -1;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool allHex(const std::string& s) {
    for (char c : s)
        if (hexValue(c) < 0) return false;
    return !s.empty();
}

uint32_t parseHex(const std::string& s) {
    uint32_t v = 0;
    for (char c : s) v = v << 4 | static_cast<uint32_t>(hexValue(c));
    return v;
}

// The 40 bits of the 8 letters, as Charles MacDonald's Game Genie notes lay them out:
//   ijklm nopIJ KLMNO PABCD EFGHd efgha bcQRS TUVWX
// address = ABCDEFGH IJKLMNOP QRSTUVWX, value = abcdefgh ijklmnop. The same shifts as
// Genesis Plus GX's decode_cheat().
bool decodeGenie(const std::string& letters, CheatPatch* out) {
    uint32_t address = 0, value = 0;
    for (int i = 0; i < 8; ++i) {
        const int v = genieValue(letters[static_cast<size_t>(i)]);
        if (v < 0) return false;
        const uint32_t n = static_cast<uint32_t>(v);
        switch (i) {
            case 0: value |= n << 3; break;
            case 1: value |= n >> 2; address |= (n & 3) << 14; break;
            case 2: address |= n << 9; break;
            case 3: address |= (n & 0xF) << 20 | (n >> 4) << 8; break;
            case 4: value |= (n & 1) << 12; address |= (n >> 1) << 16; break;
            case 5: value |= (n & 1) << 15 | (n >> 1) << 8; break;
            case 6: value |= (n >> 3) << 13; address |= (n & 7) << 5; break;
            case 7: address |= n; break;
        }
    }
    out->address = address;
    out->value = static_cast<uint16_t>(value);
    out->byte = false;
    return true;
}

bool decodeSmsCheat(const std::string& s, CheatPatch* out, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    std::string digits;
    for (char c : s)
        if (c != '-') digits += c;
    if (!allHex(digits))
        return fail(tr("Los códigos de Master System y Game Gear son de cifras hexadecimales: 00C0-1205 (Action "
                       "Replay) o 3A8-6CF-E66 (Game Genie)."));
    CheatPatch p;
    p.byte = true;
    if (digits.size() == 8) {
        // Action Replay: 00, the address, the byte.
        p.address = parseHex(digits.substr(2, 4));
        p.value = static_cast<uint16_t>(parseHex(digits.substr(6, 2)));
        if (p.address < 0xC000) return fail(tr("Un código Action Replay de Master System escribe en la RAM (C000-DFFF)."));
        p.ram = true;
        *out = p;
        return true;
    }
    if (digits.size() == 6 || digits.size() == 9) {
        // Game Genie: value, the address's low 12 bits, its high 4 bits inverted, and
        // optionally the byte it replaces, scrambled.
        auto h = [&](size_t i) { return static_cast<uint32_t>(hexValue(digits[i])); };
        p.value = static_cast<uint16_t>(h(0) << 4 | h(1));
        p.address = h(2) << 8 | h(3) << 4 | h(4) | (h(5) ^ 0xF) << 12;
        if (digits.size() == 9) {
            uint32_t ref = h(6) << 4 | h(8);
            ref = ((ref >> 2) | (ref << 6)) & 0xFF;
            p.compare = static_cast<int>(ref ^ 0xBA);
        }
        if (p.address >= 0xC000) return fail(tr("El código Game Genie apunta fuera de la ROM."));
        *out = p;
        return true;
    }
    return fail(tr("Los códigos de Master System y Game Gear son de cifras hexadecimales: 00C0-1205 (Action "
                   "Replay) o 3A8-6CF-E66 (Game Genie)."));
}

}  // namespace

std::string encodeGameGenie(uint32_t address, uint16_t value) {
    const uint32_t a = address, d = value;
    const uint32_t n[8] = {
        (d >> 3) & 31,
        ((d & 7) << 2) | ((a >> 14) & 3),
        (a >> 9) & 31,
        (((a >> 8) & 1) << 4) | ((a >> 20) & 15),
        (((a >> 16) & 15) << 1) | ((d >> 12) & 1),
        (((d >> 8) & 15) << 1) | ((d >> 15) & 1),
        (((d >> 13) & 3) << 3) | ((a >> 5) & 7),
        a & 31,
    };
    std::string s;
    for (int i = 0; i < 8; ++i) {
        if (i == 4) s += '-';
        s += kGenie[n[i]];
    }
    return s;
}

bool decodeCheat(const std::string& code, CheatPatch* out, std::string* error, bool masterSystem) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    const std::string s = normalizeCheat(code);
    if (s.empty()) return fail(tr("Escribe un código."));
    if (masterSystem) return decodeSmsCheat(s, out, error);

    // Action Replay: 6 hex digits of address, then 4 (a word) or 2 (a byte) of value,
    // with or without a ':' between them. Eight hex digits with no separator are only
    // taken as Action Replay when they point at RAM (FFxxxx), since a Game Genie code
    // can be all hex digits too.
    std::string address, value;
    const size_t colon = s.find(':');
    if (colon != std::string::npos) {
        address = s.substr(0, colon);
        value = s.substr(colon + 1);
    } else if (s.size() == 10 && allHex(s)) {
        address = s.substr(0, 6);
        value = s.substr(6);
    } else if (s.size() == 8 && allHex(s) && s.compare(0, 2, "FF") == 0) {
        address = s.substr(0, 6);
        value = s.substr(6);
    }
    if (!address.empty() || colon != std::string::npos) {
        if (address.size() != 6 || !allHex(address) || (value.size() != 4 && value.size() != 2) || !allHex(value))
            return fail(tr("Un código Action Replay es como FF0A12:0009 (6 cifras hexadecimales y luego 4, o 2 para un byte)."));
        CheatPatch p;
        p.address = parseHex(address);
        p.value = static_cast<uint16_t>(parseHex(value));
        p.byte = value.size() == 2;
        if (p.address >= 0x400000 && p.address < 0xE00000)
            return fail(tr("El código apunta fuera de la ROM y de la RAM."));
        // An odd address with a value that fits in a byte ("FF0205:0005") is a byte
        // write, as Genesis Plus GX takes it: the cheat lists are full of them.
        if (!p.byte && (p.address & 1) && p.value <= 0xFF) p.byte = true;
        if (!p.byte && (p.address & 1)) return fail(tr("Un código de 4 cifras necesita una dirección par."));
        p.ram = p.address >= 0xE00000;
        *out = p;
        return true;
    }

    std::string letters = s;
    if (letters.size() == 9 && letters[4] == '-') letters.erase(4, 1);
    if (letters.size() != 8)
        return fail(tr("Un código Game Genie tiene 8 letras (ABCD-EFGH) y uno Action Replay es como FF0A12:0009."));
    for (char c : letters)
        if (genieValue(c) < 0) return fail(tr("Los códigos Game Genie no usan las letras I, O, Q ni U."));
    CheatPatch p;
    decodeGenie(letters, &p);
    if (p.address >= 0x400000) return fail(tr("El código Game Genie apunta fuera de la ROM."));
    if (p.address & 1) return fail(tr("El código Game Genie apunta a una dirección impar."));
    *out = p;
    return true;
}

bool decodeCheatCodes(const std::string& code, std::vector<CheatPatch>* out, std::string* error, bool masterSystem) {
    return plum::decodeCheatCodes(
        code,
        [masterSystem](const std::string& part, CheatPatch* p, std::string* e) { return decodeCheat(part, p, e, masterSystem); },
        out, error);
}

}  // namespace plumsg
