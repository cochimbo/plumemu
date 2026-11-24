#pragma once

namespace plumnin {

// The console buttons that can be bound: the Super Nintendo pad's, which has them all,
// in the order of its PadLayout (frontend/buttons.cpp) and of padButtons(): the index
// of each in InputConfig's arrays.
enum class PadButton { Up, Down, Left, Right, A, B, X, Y, L, R, Select, Start, Count };

}  // namespace plumnin
