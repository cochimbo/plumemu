#include "buttons.h"

#include "frontend/input.h"

namespace plum {

const PadLayout& padLayout() {
    static_assert(static_cast<int>(plumsg::MdButton::Count) == InputConfig::kButtons);
    // Names used in the [keyboard] / [gamepad] sections of the .ini file (iniName() reads
    // the Spanish ones older versions wrote).
    static const PadLayout kLayout{
        {"up", "down", "left", "right", "a", "b", "c", "start", "x", "y", "z", "mode"},
        {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_A, SDL_SCANCODE_S,
         SDL_SCANCODE_D, SDL_SCANCODE_RETURN, SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_RSHIFT},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
         SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
         SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_Y,
         SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_INVALID},
        SDL_CONTROLLER_BUTTON_BACK,
    };
    return kLayout;
}

}  // namespace plum
