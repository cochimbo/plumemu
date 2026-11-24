#pragma once

#include "core/cheat_list.h"

#include <cstdint>
#include <string>
#include <vector>

namespace plumsg {

// CheatPatch, Cheat and the cheat file are common (core/cheat_list.h). Here, what the
// Mega Drive's and the Master System's codes mean. Addresses below 0x400000 patch the
// cartridge ROM (what the Game Genie does); addresses in work RAM (0xE00000-0xFFFFFF)
// are written again every frame (what the Action Replay does).

// Game Genie ("ABCD-EFGH", 8 letters of its 32-character alphabet) or Action Replay
// ("FF0A12:0009" word, "FF0A12:09" byte; the separator is optional). False, with a
// message in Spanish, if the code is neither or points outside the ROM and RAM.
//
// With `masterSystem`, the codes of the Master System and the Game Gear instead: Action
// Replay "00C0-1205" (00, a RAM address C000-DFFF, a byte; the dash is optional) and the
// Game Gear's Game Genie "3A8-6CF" or "3A8-6CF-E66" (with the byte it replaces). Their
// addresses are the Z80's: RAM from 0xC000, ROM below 0xC000.
bool decodeCheat(const std::string& code, CheatPatch* out, std::string* error, bool masterSystem = false);
// The same for a code made of several joined with '+' ("ABCD-EFGH+FF0A12:0009"), the
// way cheat lists write a cheat that needs more than one write: all its writes, or
// false with the message of the first part that is wrong.
bool decodeCheatCodes(const std::string& code, std::vector<CheatPatch>* out, std::string* error,
                      bool masterSystem = false);
// The Game Genie code for a ROM word patch (address must be even and below 0x400000).
std::string encodeGameGenie(uint32_t address, uint16_t value);

}  // namespace plumsg
