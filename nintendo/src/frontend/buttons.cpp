#include "buttons.h"

#include "frontend/input.h"

namespace plum {

const PadLayout& padLayout() {
    static_assert(static_cast<int>(plumnin::PadButton::Count) == InputConfig::kButtons);
    // On the pad by where they are, as on a Super Nintendo's: SDL names an Xbox pad's
    // buttons, whose bottom one (A) is where the Super Nintendo has B, and so on round.
    static const PadLayout kLayout{
        {"up", "down", "left", "right", "a", "b", "x", "y", "l", "r", "select", "start"},
        {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_X, SDL_SCANCODE_Z,
         SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RETURN},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
         SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_Y,
         SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
         SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START},
        SDL_CONTROLLER_BUTTON_INVALID,
    };
    return kLayout;
}

}  // namespace plum
