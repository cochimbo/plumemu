#pragma once

#include <cstdint>

#include "core/pad_state.h"

namespace plumnin {

// PadState (core/pad_state.h): the Super Nintendo's pad reads the cross, A, B, X, Y, L,
// R, Select and Start; the Game Boy and the NES the cross, A, B, Select and Start.

// What is plugged into a port: a pad or nothing. A console with no second port (the
// Game Boy) ignores the second.
enum class PadType : uint8_t { Pad = 0, None = 1 };
constexpr int kPadTypes = 2;

}  // namespace plumnin
