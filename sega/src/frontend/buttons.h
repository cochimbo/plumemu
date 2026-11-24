#pragma once

namespace plumsg {

// The Mega Drive pad's buttons, in the order of its PadLayout (frontend/buttons.cpp) and
// of padButtons(): the index of each in InputConfig's arrays.
enum class MdButton { Up, Down, Left, Right, A, B, C, Start, X, Y, Z, Mode, Count };

}  // namespace plumsg
