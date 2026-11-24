#pragma once

#include <array>

#include "plum.h"

namespace plum {

// The buttons of one pad, true while held: every button of the pads the programs
// emulate. Each console reads the ones its pad has: the Mega Drive the cross, A, B, C,
// X, Y, Z, Start and Mode; the Super Nintendo the cross, A, B, X, Y, L, R, Select and
// Start; the Game Boy and the NES the cross, A, B, Select and Start.
struct PadState {
    bool up = false, down = false, left = false, right = false;
    bool a = false, b = false, c = false;
    bool x = false, y = false, z = false;
    bool l = false, r = false;
    bool start = false, select = false, mode = false;
};

// The program's pad as 12 buttons, in the order netplay sends them (packPad): the
// cross first. Each program defines it (core/system.cpp).
using PadButtons = std::array<bool PadState::*, 12>;
const PadButtons& padButtons();

}  // namespace plum
