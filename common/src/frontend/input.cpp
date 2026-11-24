#include "frontend/input.h"

#include "core/app_info.h"
#include "core/i18n.h"
#include "core/ini_names.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace plum {

namespace {

// The cross: the first four buttons of every PadLayout.
constexpr int kUp = 0, kDown = 1, kLeft = 2, kRight = 3;
constexpr const char* kHotkeyNames[InputConfig::kHotkeys] = {
    "pseudo_3d", "save_state", "load_state", "previous_slot", "next_slot",
    "states_menu", "rewind", "fast_forward", "slow_motion", "library", "stop", "fps_counter"};

constexpr int kTriggerThreshold = 16000;  // of 32767: a trigger counts as pressed past halfway

bool padDown(SDL_GameController* pad, SDL_GameControllerButton b) {
    if (b == kPadLeftTrigger) return SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > kTriggerThreshold;
    if (b == kPadRightTrigger) return SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > kTriggerThreshold;
    return SDL_GameControllerGetButton(pad, b) != 0;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

int buttonIndex(const std::string& name) {
    for (int i = 0; i < InputConfig::kButtons; ++i)
        if (name == padLayout().names[static_cast<size_t>(i)]) return i;
    return -1;
}

int hotkeyIndex(const std::string& name) {
    for (int i = 0; i < InputConfig::kHotkeys; ++i)
        if (name == kHotkeyNames[i]) return i;
    return -1;
}

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

}  // namespace

const char* padButtonName(SDL_GameControllerButton b) {
    if (b == kPadLeftTrigger) return "lefttrigger";
    if (b == kPadRightTrigger) return "righttrigger";
    const char* name = SDL_GameControllerGetStringForButton(b);
    return name ? name : "?";
}

SDL_GameControllerButton padButtonFromName(const std::string& name) {
    const std::string n = lower(name);
    if (n == "lefttrigger") return kPadLeftTrigger;
    if (n == "righttrigger") return kPadRightTrigger;
    return SDL_GameControllerGetButtonFromString(n.c_str());
}

SDL_GameControllerButton triggerPulled(const SDL_Event& e) {
    if (e.type != SDL_CONTROLLERAXISMOTION || e.caxis.value <= kTriggerThreshold) return SDL_CONTROLLER_BUTTON_INVALID;
    if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) return kPadLeftTrigger;
    if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) return kPadRightTrigger;
    return SDL_CONTROLLER_BUTTON_INVALID;
}

InputConfig InputConfig::defaults() {
    InputConfig c;
    const PadLayout& layout = padLayout();
    for (size_t b = 0; b < layout.keys.size(); ++b) {
        c.keys[b] = {layout.keys[b]};
        if (layout.pad[b] != SDL_CONTROLLER_BUTTON_INVALID) c.pad[b] = {layout.pad[b]};
    }
    c.hotkeyKeys[static_cast<int>(Hotkey::Toggle3D)] = {SDL_SCANCODE_F3};
    if (layout.toggle3DPad != SDL_CONTROLLER_BUTTON_INVALID)
        c.hotkeyPad[static_cast<int>(Hotkey::Toggle3D)] = {layout.toggle3DPad};
    c.hotkeyKeys[static_cast<int>(Hotkey::SaveState)] = {SDL_SCANCODE_F5};
    c.hotkeyKeys[static_cast<int>(Hotkey::LoadState)] = {SDL_SCANCODE_F8};
    c.hotkeyKeys[static_cast<int>(Hotkey::PrevSlot)] = {SDL_SCANCODE_F6};
    c.hotkeyKeys[static_cast<int>(Hotkey::NextSlot)] = {SDL_SCANCODE_F7};
    c.hotkeyKeys[static_cast<int>(Hotkey::StateMenu)] = {SDL_SCANCODE_F4};
    c.hotkeyPad[static_cast<int>(Hotkey::StateMenu)] = {SDL_CONTROLLER_BUTTON_LEFTSTICK};
    c.hotkeyKeys[static_cast<int>(Hotkey::Rewind)] = {SDL_SCANCODE_BACKSPACE};
    c.hotkeyPad[static_cast<int>(Hotkey::Rewind)] = {kPadLeftTrigger};
    c.hotkeyKeys[static_cast<int>(Hotkey::FastForward)] = {SDL_SCANCODE_TAB};
    c.hotkeyPad[static_cast<int>(Hotkey::FastForward)] = {kPadRightTrigger};
    c.hotkeyKeys[static_cast<int>(Hotkey::SlowMotion)] = {SDL_SCANCODE_F9};
    c.hotkeyKeys[static_cast<int>(Hotkey::Library)] = {SDL_SCANCODE_F10};
    c.hotkeyKeys[static_cast<int>(Hotkey::Stop)] = {SDL_SCANCODE_F12};
    c.hotkeyKeys[static_cast<int>(Hotkey::FpsCounter)] = {SDL_SCANCODE_F};
    return c;
}

InputConfig InputConfig::loadOrCreate(const std::string& path) {
    InputConfig c = defaults();
    std::ifstream in(path);
    if (!in) {
        c.save(path);
        return c;
    }
    parse(in, c);
    return c;
}

bool InputConfig::parse(std::istream& in, InputConfig& c) {
    bool buttons = false;
    std::string line, section;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = iniName(lower(trim(line.substr(1, line.size() - 2))));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = iniName(lower(trim(line.substr(0, eq))));
        const std::string value = trim(line.substr(eq + 1));

        if (section == "gamepad" && key == "stick_as_dpad") {
            c.stickAsDpad = value == "1" || iniName(lower(value)) == "yes" || lower(value) == "true";
            continue;
        }
        if (section == "gamepad" && key == "deadzone") {
            c.stickDeadzone = std::clamp(std::atoi(value.c_str()), 0, 32767);
            continue;
        }
        const int b = buttonIndex(key);
        const int h = hotkeyIndex(key);
        if (b < 0 && h < 0) continue;
        if (section == "keyboard2" && b >= 0) {
            auto& list = c.keys2[b];
            list.clear();
            for (const auto& name : splitList(value)) {
                SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
                if (sc != SDL_SCANCODE_UNKNOWN) list.push_back(sc);
            }
        } else if (section == "keyboard") {
            auto& list = b >= 0 ? c.keys[b] : c.hotkeyKeys[h];
            list.clear();
            for (const auto& name : splitList(value)) {
                SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
                if (sc != SDL_SCANCODE_UNKNOWN) list.push_back(sc);
            }
        } else if (section == "gamepad") {
            auto& list = b >= 0 ? c.pad[b] : c.hotkeyPad[h];
            list.clear();
            for (const auto& name : splitList(value)) {
                SDL_GameControllerButton gb = padButtonFromName(name);
                if (gb != SDL_CONTROLLER_BUTTON_INVALID) list.push_back(gb);
            }
        } else {
            continue;
        }
        buttons |= b >= 0;
    }
    return buttons;
}

bool InputConfig::save(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    write(out);
    return static_cast<bool>(out);
}

namespace {

void writeKeyList(std::ostream& out, const char* name, const std::vector<SDL_Scancode>& list) {
    out << name << " = ";
    for (size_t k = 0; k < list.size(); ++k) out << (k ? ", " : "") << SDL_GetScancodeName(list[k]);
    out << "\n";
}

void writePadList(std::ostream& out, const char* name, const std::vector<SDL_GameControllerButton>& list) {
    out << name << " = ";
    for (size_t k = 0; k < list.size(); ++k) out << (k ? ", " : "") << padButtonName(list[k]);
    out << "\n";
}

}  // namespace

void InputConfig::writeButtons(std::ostream& out) const {
    out << "[keyboard]\n";
    for (int i = 0; i < kButtons; ++i) writeKeyList(out, padLayout().names[static_cast<size_t>(i)], keys[i]);
    out << "\n[keyboard2]\n";
    for (int i = 0; i < kButtons; ++i) writeKeyList(out, padLayout().names[static_cast<size_t>(i)], keys2[i]);
    out << "\n[gamepad]\n";
    for (int i = 0; i < kButtons; ++i) writePadList(out, padLayout().names[static_cast<size_t>(i)], pad[i]);
}

void InputConfig::copyButtons(const InputConfig& from) {
    keys = from.keys;
    keys2 = from.keys2;
    pad = from.pad;
}

void InputConfig::write(std::ostream& out) const {
    auto keyList = [&](const char* name, const std::vector<SDL_Scancode>& list) { writeKeyList(out, name, list); };
    auto padList = [&](const char* name, const std::vector<SDL_GameControllerButton>& list) {
        writePadList(out, name, list);
    };
    out << "# " << appInfo().name << " controls. You can give several keys or buttons separated by commas.\n"
           "# [keyboard] is player 1 and [keyboard2] player 2; the first gamepad is player 1\n"
           "# and the second, player 2.\n"
           "# Keys: SDL names (Up, Down, Left, Right, A, S, Return, Space, Right Shift...).\n"
           "# Gamepad buttons: a, b, x, y, back, guide, start, leftstick, rightstick,\n"
           "# leftshoulder, rightshoulder, lefttrigger, righttrigger, dpup, dpdown, dpleft, dpright.\n"
           "# pseudo_3d turns the pseudo-3D effect on and off; save_state, load_state, previous_slot\n"
           "# and next_slot handle save states; states_menu opens a menu to choose a slot, save and\n"
           "# load with the gamepad. rewind and fast_forward work while held; slow_motion turns it on\n"
           "# and off. library opens the game list with its covers. stop closes the game and goes\n"
           "# back to the start screen (or the library). fps_counter shows the FPS counter.\n\n[keyboard]\n";
    for (int i = 0; i < kButtons; ++i) keyList(padLayout().names[static_cast<size_t>(i)], keys[i]);
    for (int i = 0; i < kHotkeys; ++i) keyList(kHotkeyNames[i], hotkeyKeys[i]);
    out << "\n[keyboard2]\n";
    for (int i = 0; i < kButtons; ++i) keyList(padLayout().names[static_cast<size_t>(i)], keys2[i]);
    out << "\n[gamepad]\n";
    for (int i = 0; i < kButtons; ++i) padList(padLayout().names[static_cast<size_t>(i)], pad[i]);
    for (int i = 0; i < kHotkeys; ++i) padList(kHotkeyNames[i], hotkeyPad[i]);
    out << "stick_as_dpad = " << (stickAsDpad ? "yes" : "no") << "\n";
    out << "deadzone = " << stickDeadzone << "\n";
}

Input::~Input() {
    for (auto* p : pads_) SDL_GameControllerClose(p);
}

void Input::setGameButtons(const InputConfig* buttons) {
    if (buttons) gameButtons_ = *buttons;
    else gameButtons_.reset();
}

void Input::openAll() {
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) continue;
        if (SDL_GameController* p = SDL_GameControllerOpen(i)) pads_.push_back(p);
    }
}

void Input::handleEvent(const SDL_Event& e) {
    pulled_ = SDL_CONTROLLER_BUTTON_INVALID;
    if (e.type == SDL_CONTROLLERAXISMOTION &&
        (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
        auto it = std::find_if(triggers_.begin(), triggers_.end(), [&](const auto& t) { return t.first == e.caxis.which; });
        if (it == triggers_.end()) it = triggers_.insert(triggers_.end(), {e.caxis.which, 0});
        const int bit = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? 1 : 2;
        const bool down = e.caxis.value > kTriggerThreshold;
        if (down && !(it->second & bit)) pulled_ = triggerPulled(e);
        it->second = down ? it->second | bit : it->second & ~bit;
        return;
    }
    if (e.type == SDL_CONTROLLERDEVICEADDED) {
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(e.cdevice.which);
        for (auto* p : pads_)
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p)) == id) return;
        if (SDL_GameController* p = SDL_GameControllerOpen(e.cdevice.which)) pads_.push_back(p);
    } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
        auto it = std::find_if(pads_.begin(), pads_.end(), [&](SDL_GameController* p) {
            return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p)) == e.cdevice.which;
        });
        if (it != pads_.end()) {
            SDL_GameControllerClose(*it);
            pads_.erase(it);
        }
    }
}

Hotkey Input::hotkeyFor(const SDL_Event& e) const {
    for (int h = 0; h < InputConfig::kHotkeys; ++h) {
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            for (SDL_Scancode sc : config_.hotkeyKeys[h])
                if (sc == e.key.keysym.scancode) return static_cast<Hotkey>(h);
        } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            for (SDL_GameControllerButton gb : config_.hotkeyPad[h])
                if (gb == e.cbutton.button) return static_cast<Hotkey>(h);
        } else if (e.type == SDL_CONTROLLERAXISMOTION && pulled_ != SDL_CONTROLLER_BUTTON_INVALID) {
            for (SDL_GameControllerButton gb : config_.hotkeyPad[h])
                if (gb == pulled_) return static_cast<Hotkey>(h);
        }
    }
    return Hotkey::Count;
}

bool Input::held(Hotkey h, bool useKeyboard) const {
    const int i = static_cast<int>(h);
    if (useKeyboard) {
        const Uint8* kb = SDL_GetKeyboardState(nullptr);
        for (SDL_Scancode sc : config_.hotkeyKeys[i])
            if (kb[sc]) return true;
    }
    for (auto* p : pads_)
        for (SDL_GameControllerButton gb : config_.hotkeyPad[i])
            if (padDown(p, gb)) return true;
    return false;
}

std::vector<std::string> Input::padNames() const {
    std::vector<std::string> names;
    for (auto* p : pads_) {
        const char* n = SDL_GameControllerName(p);
        names.push_back(n ? n : tr("Mando"));
    }
    return names;
}

void Input::firstStick(int axisX, int axisY, float* x, float* y) const {
    *x = *y = 0;
    for (auto* p : pads_) {
        const int ax = SDL_GameControllerGetAxis(p, static_cast<SDL_GameControllerAxis>(axisX));
        const int ay = SDL_GameControllerGetAxis(p, static_cast<SDL_GameControllerAxis>(axisY));
        if (std::abs(ax) < config_.stickDeadzone && std::abs(ay) < config_.stickDeadzone) continue;
        *x = static_cast<float>(ax) / 32767.0f;
        *y = static_cast<float>(ay) / 32767.0f;
        return;
    }
}

void Input::rightStick(float* x, float* y) const {
    firstStick(SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY, x, y);
}

void Input::leftStick(float* x, float* y) const {
    firstStick(SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, x, y);
}

void Input::leftStick(int player, float* x, float* y) const {
    *x = *y = 0;
    if (player < 0 || static_cast<size_t>(player) >= pads_.size()) return;
    SDL_GameController* p = pads_[static_cast<size_t>(player)];
    const int ax = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX);
    const int ay = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
    if (std::abs(ax) < config_.stickDeadzone && std::abs(ay) < config_.stickDeadzone) return;
    *x = static_cast<float>(ax) / 32767.0f;
    *y = static_cast<float>(ay) / 32767.0f;
}

std::array<bool, InputConfig::kButtons> Input::buttonsDown(int player, bool useKeyboard) const {
    std::array<bool, InputConfig::kButtons> down{};
    const Uint8* kb = SDL_GetKeyboardState(nullptr);
    const InputConfig& buttons = gameButtons_ ? *gameButtons_ : config_;
    const auto& keys = player == 0 ? buttons.keys : buttons.keys2;
    SDL_GameController* pad = static_cast<size_t>(player) < pads_.size() ? pads_[static_cast<size_t>(player)] : nullptr;
    for (int b = 0; b < InputConfig::kButtons; ++b) {
        if (useKeyboard)
            for (SDL_Scancode sc : keys[b]) down[b] = down[b] || kb[sc];
        if (pad)
            for (SDL_GameControllerButton gb : buttons.pad[b]) down[b] = down[b] || padDown(pad, gb);
    }
    if (config_.stickAsDpad && pad) {
        const int x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        const int y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        down[kLeft] |= x < -config_.stickDeadzone;
        down[kRight] |= x > config_.stickDeadzone;
        down[kUp] |= y < -config_.stickDeadzone;
        down[kDown] |= y > config_.stickDeadzone;
    }
    return down;
}

void Input::ignoreHeld() {
    for (int player = 0; player < 2; ++player) ignored_[static_cast<size_t>(player)] = buttonsDown(player, true);
}

PadState Input::poll(int player, bool useKeyboard) const {
    std::array<bool, InputConfig::kButtons> down = buttonsDown(player, useKeyboard);
    // A button ignored since a menu closed comes back once it is let go (on the keyboard
    // too, even while the keyboard is not being read).
    auto& ignored = ignored_[static_cast<size_t>(player & 1)];
    if (std::find(ignored.begin(), ignored.end(), true) != ignored.end()) {
        const std::array<bool, InputConfig::kButtons> held = useKeyboard ? down : buttonsDown(player, true);
        for (int b = 0; b < InputConfig::kButtons; ++b) {
            ignored[b] = ignored[b] && held[b];
            down[b] = down[b] && !ignored[b];
        }
    }
    const PadButtons& buttons = padButtons();
    PadState out;
    for (size_t b = 0; b < buttons.size(); ++b) out.*buttons[b] = down[b];
    return out;
}

}  // namespace plum
