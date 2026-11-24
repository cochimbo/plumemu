// The states menu (the pad's menu while a game runs) and its slots.
#include "frontend/ui.h"

#include "frontend/handheld.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>

#include "imgui.h"
#include "imgui_internal.h"  // ImGui::TableSetColumnSortDirection
#include "frontend/game_options.h"
#include "frontend/languages.h"
#include "frontend/library_names.h"
#include "frontend/theme.h"
#include "core/app_info.h"
#include "core/i18n.h"
#include "core/system.h"
#include "net/udp.h"
#include "render/box_shelf.h"
#include "render/gl.h"

#include "stb_image.h"
#include "frontend/ui_helpers.h"

namespace PLUM_APP_NAMESPACE {

using namespace ui_helpers;

void Ui::toggleStateMenu(int tab) {
    stateMenu_.open = !stateMenu_.open;
    if (tab >= 0 && tab < StateMenu::kTabs) stateMenu_.tab = tab;
    stateMenu_.selected = settings_.stateSlot;
    stateMenu_.scroll = true;
    stateMenu_.changes.clear();
    stateMenu_.held = 0;
    stateMenu_.tabChanged = true;  // it opens on the tab it was left on
}

// While the states menu is open it owns the pad and the keyboard: L1 and R1 (or Q and E)
// change tab. On the slots, up and down pick a slot, A (or Enter) loads it, Y (or Space)
// saves to it; on the tabs of options, up and down pick an option, left and right change
// it and A (or Enter) switches it. B (or Esc) closes the menu.
bool Ui::handleStateMenu(const SDL_Event& e) {
    if (e.type != SDL_KEYDOWN && e.type != SDL_CONTROLLERBUTTONDOWN && e.type != SDL_KEYUP &&
        e.type != SDL_CONTROLLERBUTTONUP && e.type != SDL_TEXTINPUT)
        return false;
    if (e.type == SDL_CONTROLLERBUTTONUP) {
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT || e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
            stateMenu_.held = 0;
        return true;
    }
    if (e.type != SDL_KEYDOWN && e.type != SDL_CONTROLLERBUTTONDOWN) return true;
    if (input_.hotkeyFor(e) == Hotkey::StateMenu) {
        stateMenu_.open = false;
        return true;
    }
    enum { None, Up, Down, Left, Right, Load, Save, Close, PrevTab, NextTab } action = None;
    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.scancode) {
            case SDL_SCANCODE_UP: action = Up; break;
            case SDL_SCANCODE_DOWN: action = Down; break;
            case SDL_SCANCODE_LEFT: action = Left; break;
            case SDL_SCANCODE_RIGHT: action = Right; break;
            case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: action = Load; break;
            case SDL_SCANCODE_SPACE: action = Save; break;
            case SDL_SCANCODE_ESCAPE: action = Close; break;
            case SDL_SCANCODE_Q: case SDL_SCANCODE_PAGEUP: action = PrevTab; break;
            case SDL_SCANCODE_E: case SDL_SCANCODE_PAGEDOWN: action = NextTab; break;
            default: break;
        }
    } else {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP: action = Up; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: action = Down; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: action = Left; break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: action = Right; break;
            case SDL_CONTROLLER_BUTTON_A: action = Load; break;
            case SDL_CONTROLLER_BUTTON_Y: action = Save; break;
            case SDL_CONTROLLER_BUTTON_B: action = Close; break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: action = PrevTab; break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: action = NextTab; break;
            default: break;
        }
        // The pad does not repeat a held button as the keyboard does: drawStateMenu() does it.
        if (action == Left || action == Right) {
            stateMenu_.held = action == Left ? -1 : 1;
            stateMenu_.heldSince = SDL_GetTicks();
            stateMenu_.repeatAt = stateMenu_.heldSince + 400;
        }
    }
    constexpr int kTabs = StateMenu::kTabs;
    if (action == PrevTab || action == NextTab) {
        stateMenu_.tab = (stateMenu_.tab + (action == PrevTab ? kTabs - 1 : 1)) % kTabs;
        stateMenu_.tabChanged = true;
        stateMenu_.option = 0;
        stateMenu_.changes.clear();
        stateMenu_.scroll = true;
        return true;
    }
    if (action == Close) {
        stateMenu_.open = false;
        return true;
    }
    if (stateMenu_.tab != 0) {
        const int count = std::max(1, stateMenu_.options);
        switch (action) {
            case Up: stateMenu_.option = (stateMenu_.option + count - 1) % count; stateMenu_.scroll = true; break;
            case Down: stateMenu_.option = (stateMenu_.option + 1) % count; stateMenu_.scroll = true; break;
            case Left: stateMenu_.changes.push_back(-1); break;
            case Right: stateMenu_.changes.push_back(1); break;
            case Load: stateMenu_.changes.push_back(0); break;
            default: break;
        }
        return true;
    }
    const int count = std::max(1, stateMenu_.slots);
    switch (action) {
        case Up: stateMenu_.selected = (stateMenu_.selected + count - 1) % count; stateMenu_.scroll = true; break;
        case Down: stateMenu_.selected = (stateMenu_.selected + 1) % count; stateMenu_.scroll = true; break;
        case Load: stateMenu_.load = stateMenu_.selected; stateMenu_.open = false; break;
        case Save: stateMenu_.save = stateMenu_.selected; stateMenu_.open = false; break;
        default: break;
    }
    if (action == Load || action == Save) {
        settings_.stateSlot = stateMenu_.selected;
        pendingSave_ = true;
    }
    return true;
}

void Ui::drawStateMenu(const UiState& state, UiActions& out) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(vp->Size.x - 20.0f, theme::px(520.0f)), std::min(vp->Size.y - 40.0f, theme::px(540.0f))),
                             ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
    if (!ImGui::Begin(tr("Estados"), &stateMenu_.open, flags)) {
        ImGui::End();
        return;
    }
    // The pad's left or right held down repeats, faster than the keyboard's.
    if (stateMenu_.held && stateMenu_.tab != 0 && SDL_GetTicks() >= stateMenu_.repeatAt) {
        stateMenu_.changes.push_back(stateMenu_.held);
        stateMenu_.repeatAt = SDL_GetTicks() + 70;
    }
    static const char* const kTabNames[StateMenu::kTabs] = {N_("Estados"), N_("Imagen"), "3D", N_("Sonido"),
                                                            N_("Velocidad"), N_("Sistema")};
    if (ImGui::BeginTabBar("pestañas")) {
        for (int t = 0; t < StateMenu::kTabs; ++t) {
            const ImGuiTabItemFlags tabFlags =
                stateMenu_.tabChanged && stateMenu_.tab == t ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(tr(kTabNames[t]), nullptr, tabFlags)) {
                // Clicked with the mouse.
                if (!stateMenu_.tabChanged && stateMenu_.tab != t) {
                    stateMenu_.tab = t;
                    stateMenu_.option = 0;
                    stateMenu_.changes.clear();
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    stateMenu_.tabChanged = false;
    theme::hint(tr("L1 / R1 (o Q / E): cambiar de pestaña   B / Esc: cerrar"));
    if (stateMenu_.tab == 0) drawStateSlots(state);
    else drawStateOptions(stateMenu_.tab, state, out);
    ImGui::End();
}

void Ui::drawStateSlots(const UiState& state) {
    if (!state.romLoaded) ImGui::TextDisabled("%s", tr("Abre una ROM para guardar estados."));
    theme::hint(tr("A / Intro: cargar   Y / Espacio: guardar"));
    ImGui::Separator();
    ImGui::BeginChild("ranuras");
    const float thumbH = 56.0f;
    for (int i = 0; i < static_cast<int>(state.slots.size()); ++i) {
        const SlotInfo& info = state.slots[static_cast<size_t>(i)];
        ImGui::PushID(i);
        const bool selected = i == stateMenu_.selected;
        const ImVec2 start = ImGui::GetCursorPos();
        // The whole row is clickable with the mouse too: a click selects, a double click loads.
        if (ImGui::Selectable("##fila", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, thumbH))) {
            stateMenu_.selected = i;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && info.used && state.romLoaded) {
                stateMenu_.load = i;
                stateMenu_.open = false;
                settings_.stateSlot = i;
                pendingSave_ = true;
            }
        }
        if (selected && stateMenu_.scroll) {
            ImGui::SetScrollHereY(0.5f);
            stateMenu_.scroll = false;
        }
        ImGui::SetCursorPos(start);
        const float thumbW = info.height ? thumbH * static_cast<float>(info.width) / static_cast<float>(info.height) : thumbH * 1.43f;
        if (info.texture) ImGui::Image(static_cast<ImTextureID>(info.texture), ImVec2(thumbW, thumbH));
        else ImGui::Dummy(ImVec2(thumbW, thumbH));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text(i == settings_.stateSlot ? tr("Ranura %d  (actual)") : tr("Ranura %d"), i);
        if (info.used) ImGui::TextUnformatted(info.when.c_str());
        else ImGui::TextDisabled("%s", tr("vacía"));
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

}  // namespace PLUM_APP_NAMESPACE
