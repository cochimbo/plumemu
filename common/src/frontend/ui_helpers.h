#pragma once

// What each program's ui.cpp and the common ui_*.cpp share: small widgets, the states
// menu's options and the library's marks. Built with each program's types (Settings,
// InputConfig...), so only its ui files include it.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include <SDL.h>

#include "imgui.h"
#include "core/i18n.h"
#include "core/video_frame.h"
#include "frontend/ui.h"
#include "render/gl.h"
#include "render/profile3d.h"
#include "stb_image.h"

namespace PLUM_APP_NAMESPACE {
namespace ui_helpers {

constexpr int kRows = InputConfig::kButtons + InputConfig::kHotkeys;
// The names of the console's buttons and of the hotkeys, as the controls window lists
// them: each program's ui.cpp has its own.
extern const char* const kRowNames[kRows];
// While a game runs the mouse pointer goes away once it has been still this long.
constexpr Uint32 kCursorIdleMs = 2000;

inline std::vector<SDL_Scancode>& keysOf(InputConfig& c, int row, int player) {
    if (row >= InputConfig::kButtons) return c.hotkeyKeys[row - InputConfig::kButtons];
    return player == 0 ? c.keys[row] : c.keys2[row];
}

inline std::vector<SDL_GameControllerButton>& padOf(InputConfig& c, int row) {
    return row < InputConfig::kButtons ? c.pad[row] : c.hotkeyPad[row - InputConfig::kButtons];
}

inline std::string fileName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// A code of several joined with '+' shown as its first and how many more ("AJ0T-CA9Y +5").
inline std::string shortCode(const std::string& code) {
    const size_t plus = code.find('+');
    if (plus == std::string::npos) return code;
    size_t more = 0;
    for (char c : code) more += c == '+';
    return code.substr(0, plus) + " +" + std::to_string(more);
}

// ImGui::Combo with its items translated.
// The widescreen's controls: a button per ready-made width and a slider per side. They
// change the picture at once; the return value says when to save.
inline bool widescreenControls(int& left, int& right) {
    bool changed = false;
    for (int i = 0; i < kWidescreenCount; ++i) {
        if (i) ImGui::SameLine();
        const Widescreen& w = kWidescreens[i];
        if (ImGui::RadioButton(w.name, left == w.columns && right == w.columns)) {
            left = right = w.columns;
            changed = true;
        }
    }
    ImGui::SliderInt(tr("Izquierda##ancha"), &left, 0, VideoFrame::kMaxWideSide, "%d px", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderInt(tr("Derecha##ancha"), &right, 0, VideoFrame::kMaxWideSide, "%d px", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    return changed;
}

// An eighth note in a `size` square at `at`: a filled head, a stem and its flag (the
// fonts may not have the glyph).
inline void drawMusicNote(ImDrawList* draw, ImVec2 at, float size, ImU32 colour) {
    const float r = size * 0.2f;
    const ImVec2 head(at.x + size * 0.38f, at.y + size * 0.78f);
    draw->AddEllipseFilled(head, ImVec2(r * 1.25f, r), colour, -0.4f);
    const float stemX = head.x + r * 1.05f;
    const float top = at.y + size * 0.08f;
    draw->AddLine(ImVec2(stemX, head.y - r * 0.3f), ImVec2(stemX, top), colour, std::max(1.0f, size * 0.09f));
    draw->AddBezierQuadratic(ImVec2(stemX, top), ImVec2(stemX + size * 0.38f, top + size * 0.2f),
                             ImVec2(stemX + size * 0.22f, top + size * 0.5f), colour, std::max(1.0f, size * 0.09f));
}

// A lightning bolt in a `size` square at `at`, the mark of a game with cheats on: two
// triangles, drawn rather than a glyph the fonts may not have.
inline void drawCheatIcon(ImDrawList* draw, ImVec2 at, float size, ImU32 colour) {
    auto p = [&](float x, float y) { return ImVec2(at.x + x * size, at.y + y * size); };
    draw->AddTriangleFilled(p(0.66f, 0.02f), p(0.16f, 0.58f), p(0.54f, 0.58f), colour);
    draw->AddTriangleFilled(p(0.46f, 0.42f), p(0.84f, 0.42f), p(0.34f, 0.98f), colour);
}
constexpr ImU32 kCheatColour = IM_COL32(110, 230, 150, 255);

// A five-pointed star in a `size` square at `at`, the mark of a favourite: a fan of
// triangles from its centre (the star is not convex).
inline void drawStar(ImDrawList* draw, ImVec2 at, float size, ImU32 colour) {
    const ImVec2 c(at.x + size * 0.5f, at.y + size * 0.53f);
    ImVec2 p[10];
    for (int i = 0; i < 10; ++i) {
        const float r = size * (i % 2 ? 0.21f : 0.5f);
        const float a = -1.5707963f + static_cast<float>(i) * 0.62831853f;
        p[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
    }
    for (int i = 0; i < 10; ++i) draw->AddTriangleFilled(c, p[i], p[(i + 1) % 10], colour);
}
constexpr ImU32 kFavoriteColour = IM_COL32(255, 196, 60, 255);

// The library's tabs, in order: every game, the ones played lately, the favourites and
// then each console.
inline std::vector<int> libraryTabs() {
    std::vector<int> tabs = {Settings::kTabAll, Settings::kTabHistory, Settings::kTabFavorites};
    for (int i = 0; i < kSystemCount; ++i) tabs.push_back(i);
    return tabs;
}

// Back to the default look of the pseudo-3D (the effect itself stays on or off).
inline void reset3D(RenderOptions& r) {
    const RenderOptions defaults;
    r.style = defaults.style;
    r.depth = defaults.depth;
    r.tilt = defaults.tilt;
    r.yaw = defaults.yaw;
    r.zoom = defaults.zoom;
    r.extrusion = defaults.extrusion;
    r.thickness = defaults.thickness;
    r.shadows = defaults.shadows;
    r.tiltCamera = defaults.tiltCamera;
    r.lighting = defaults.lighting;
    r.liveCamera = defaults.liveCamera;
    r.highBInFront = defaults.highBInFront;
    r.flatB = defaults.flatB;
}

inline bool comboTr(const char* label, int* current, const char* const items[], int count) {
    std::vector<const char*> shown(items, items + count);
    for (const char*& item : shown) item = tr(item);
    return ImGui::Combo(label, current, shown.data(), count);
}

// The run-ahead frames, 0 (off) to Settings::kMaxRunAhead.
inline bool runAheadSlider(const char* label, int& frames) {
    const char* shown = frames == 0 ? tr("desactivado") : frames == 1 ? tr("1 fotograma") : tr("%d fotogramas");
    return ImGui::SliderInt(label, &frames, 0, Settings::kMaxRunAhead, shown, ImGuiSliderFlags_AlwaysClamp);
}

// The overclock's choices, as percent of the console's processor speed.
constexpr int kOverclocks[] = {100, 150, 200, 300, 400};
constexpr const char* kOverclockNames[] = {N_("Desactivado (velocidad real)"), "150 %", "200 %", "300 %", "400 %"};

// The combo for the overclock; `percent` takes the chosen value.
// An on/off option as a "Sí" / "No" list, so a row of the game's options has no second
// box next to the one that says whether the game has its own value.
inline bool yesNo(const char* label, bool& value) {
    static const char* const names[] = {N_("No"), N_("Sí")};
    int i = value ? 1 : 0;
    if (!comboTr(label, &i, names, 2)) return false;
    value = i == 1;
    return true;
}

inline bool overclockCombo(const char* label, int& percent) {
    int i = 0;
    for (int k = 0; k < static_cast<int>(std::size(kOverclocks)); ++k)
        if (percent >= kOverclocks[k]) i = k;
    if (!comboTr(label, &i, kOverclockNames, static_cast<int>(std::size(kOverclocks)))) return false;
    percent = kOverclocks[i];
    return true;
}

// One line of the states menu's tabs of options: what it is, its value, and what left
// (-1), right (+1) and A or a double click (0) do to it.
struct MenuOption {
    std::string label;
    std::string value;
    bool enabled = true;
    std::function<void(int)> change;
    int effects = 0;  // what a change asks main() for, MenuEffect bits
    bool arrows = true;  // false for the ones that only do something with A, and have no value
};

enum MenuEffect {
    kSave = 1,          // write the program's .ini
    kSound = 2,         // the Roland sound was switched or retuned (plumsg)
    kEqualizer = 4,
    kRewind = 8,        // the rewind length changed
    kFullscreen = 16,
};

inline const char* onOff(bool on) { return on ? tr("activado") : tr("desactivado"); }

inline std::string formatted(const char* format, double value) {
    char text[32];
    std::snprintf(text, sizeof text, format, value);
    return text;
}

inline MenuOption toggle(const char* label, bool& value, int effects = kSave, bool enabled = true) {
    return {tr(label), onOff(value), enabled, [&value](int) { value = !value; }, effects};
}

// A number from `low` to `high` in steps of `step`, shown with the printf `format` (of a double).
inline MenuOption number(const char* label, int& value, int low, int high, int step, const char* format, int effects = kSave,
                  bool enabled = true) {
    return {tr(label), formatted(format, value), enabled,
            [&value, low, high, step](int dir) { value = std::clamp(value + (dir ? dir : 1) * step, low, high); },
            effects};
}

// The same for a float, 0..1 shown as a percentage unless `scale` says otherwise.
inline MenuOption amount(const char* label, float& value, float low, float high, float step, const char* format,
                  float scale = 100.0f, int effects = kSave, bool enabled = true) {
    return {tr(label), formatted(format, value * scale), enabled,
            [&value, low, high, step](int dir) {
                // Rounded to the step, so that the values shown stay round.
                const float v = std::round(value / step) * step + static_cast<float>(dir ? dir : 1) * step;
                value = std::clamp(v, low, high);
            },
            effects};
}

// One of `count` choices `names` (translated), going round: `current` is the one now,
// and `set` takes the new one.
inline MenuOption choice(const char* label, int current, const char* const names[], int count, std::function<void(int)> set,
                  int effects = kSave, bool enabled = true) {
    current = std::clamp(current, 0, count - 1);
    return {tr(label), tr(names[current]), enabled,
            [current, count, set = std::move(set)](int dir) { set((current + (dir < 0 ? count - 1 : 1)) % count); },
            effects};
}

constexpr const char* kFpsCornerNames[] = {N_("Arriba a la izquierda"), N_("Arriba a la derecha"),
                                           N_("Abajo a la izquierda"), N_("Abajo a la derecha")};
static_assert(std::size(kFpsCornerNames) == Settings::kFpsCorners, "a name per corner");

// The interface's sizes to choose from, in percent (0 automatic).
constexpr int kUiScales[] = {0, 75, 100, 125, 150, 175, 200, 250};
constexpr int kUiScaleCount = static_cast<int>(std::size(kUiScales));
inline std::string uiScaleName(int i) {
    return kUiScales[i] ? std::to_string(kUiScales[i]) + " %" : std::string(tr("Automático"));
}
// Settings::consoleMode's: automatic (saying why it is on, if it is), on, off.
inline std::string consoleModeName(int mode) {
    if (mode == 1) return tr("Activado");
    if (mode == 2) return tr("Desactivado");
    const std::string& why = consoleModeReason();
    return why.empty() ? std::string(tr("Automático")) : trf("Automático (%s)", why.c_str());
}

constexpr const char* kRegionNames[] = {N_("Automática (la de la ROM)"), N_("Japón (NTSC)"), N_("EE. UU. (NTSC)"),
                                        N_("Europa (PAL)")};


inline bool fileExists(const char* path) {
    SDL_RWops* f = SDL_RWFromFile(path, "rb");
    if (f) SDL_RWclose(f);
    return f != nullptr;
}


// An RGBA texture from an embedded PNG; 0 if it cannot be decoded. With `window`, the
// image is also the window's icon.
inline unsigned int textureFromPng(const unsigned char* png, size_t size, int* w, int* h, SDL_Window* window = nullptr) {
    int n = 0;
    stbi_uc* rgba = stbi_load_from_memory(png, static_cast<int>(size), w, h, &n, 4);
    if (!rgba) return 0;
    unsigned int texture = 0;
    gl::GenTextures(1, &texture);
    gl::BindTexture(gl::TEXTURE_2D, texture);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, *w, *h, 0, gl::RGBA, gl::UNSIGNED_BYTE, rgba);
    if (window) {
        if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(rgba, *w, *h, 32, *w * 4, SDL_PIXELFORMAT_RGBA32)) {
            SDL_SetWindowIcon(window, s);
            SDL_FreeSurface(s);
        }
    }
    stbi_image_free(rgba);
    return texture;
}

}  // namespace ui_helpers
}  // namespace PLUM_APP_NAMESPACE
