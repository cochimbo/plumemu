// The controls window and a game's own controls and options.
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

bool Ui::drawBindings(InputConfig& c, bool game) {
    bool changed = false;
    if (ImGui::BeginTable(game ? "game_bindings" : "bindings", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn(tr("Acción"), ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn(tr("Teclado (jugador 1)"));
        ImGui::TableSetupColumn(tr("Teclado (jugador 2)"));
        ImGui::TableSetupColumn(tr("Mando"));
        ImGui::TableHeadersRow();
        for (int row = 0; row < (game ? InputConfig::kButtons : kRows); ++row) {
            ImGui::PushID(row);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tr(kRowNames[row]));
            for (int col = 0; col < 3; ++col) {
                const bool pad = col == 2;
                ImGui::TableNextColumn();
                if (col == 1 && row >= InputConfig::kButtons) continue;  // hotkeys: one keyboard list
                ImGui::PushID(col);
                std::string label;
                if (capture_.active && capture_.game == game && capture_.row == row && capture_.column == col) {
                    label = pad ? tr("Pulsa un botón...") : tr("Pulsa una tecla...");
                } else if (pad) {
                    for (auto b : padOf(c, row)) label += (label.empty() ? "" : ", ") + std::string(padButtonName(b));
                } else {
                    for (auto k : keysOf(c, row, col)) label += (label.empty() ? "" : ", ") + std::string(SDL_GetScancodeName(k));
                }
                if (label.empty()) label = "-";
                const float small = ImGui::GetFrameHeight() * 2 + ImGui::GetStyle().ItemSpacing.x * 2;
                if (ImGui::Button(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - small, 0)))
                    capture_ = {true, row, col, false, game};
                ImGui::SameLine();
                if (ImGui::Button("+", ImVec2(ImGui::GetFrameHeight(), 0))) capture_ = {true, row, col, true, game};
                ImGui::SameLine();
                if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0))) {
                    if (pad) padOf(c, row).clear();
                    else keysOf(c, row, col).clear();
                    capture_.active = false;
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    return changed;
}

void Ui::drawControls(UiActions& out) {
    ImGui::SetNextWindowSize(ImVec2(theme::px(760), 0), ImGuiCond_Appearing);
    if (!ImGui::Begin(tr("Controles"), &showControls_)) {
        ImGui::End();
        return;
    }
    if (!showControls_ && !capture_.game) capture_.active = false;

    ImGui::TextWrapped("%s", tr("Pulsa una casilla y luego la tecla o el botón del mando. \"+\" añade otro, "
                                "\"x\" lo borra, Esc cancela."));
    if (input_.hasGameButtons())
        ImGui::TextDisabled("%s", tr("El juego abierto tiene sus propios botones (en Opciones de este juego); "
                                     "aquí cambias los generales."));
    InputConfig c = input_.config();
    bool changed = drawBindings(c, false);

    ImGui::Spacing();
    changed |= ImGui::Checkbox(tr("Usar el stick izquierdo como cruceta"), &c.stickAsDpad);
    ImGui::BeginDisabled(!c.stickAsDpad);
    int dead = c.stickDeadzone * 100 / 32767;
    if (ImGui::SliderInt(tr("Zona muerta del stick"), &dead, 0, 95, "%d%%")) {
        c.stickDeadzone = dead * 32767 / 100;
        changed = true;
    }
    ImGui::EndDisabled();

    const auto pads = input_.padNames();
    if (pads.empty()) {
        ImGui::TextDisabled("%s", tr("No hay mandos conectados."));
    } else {
        ImGui::Text("%s", tr("Mandos conectados:"));
        for (const auto& p : pads) ImGui::BulletText("%s", p.c_str());
    }

    ImGui::Spacing();
    if (ImGui::Button(tr("Restaurar predeterminados"))) {
        const InputConfig d = InputConfig::defaults();
        c.keys = d.keys;
        c.keys2 = d.keys2;
        c.pad = d.pad;
        c.hotkeyKeys = d.hotkeyKeys;
        c.hotkeyPad = d.hotkeyPad;
        c.stickAsDpad = d.stickAsDpad;
        c.stickDeadzone = d.stickDeadzone;
        capture_.active = false;
        changed = true;
    }
    if (changed) {
        input_.setConfig(std::move(c));
        out.settingsChanged = true;
    }
    ImGui::End();
}

// The game's own console buttons, which take the place of the general ones (Opciones >
// Controles) while it is loaded. The hotkeys and the stick stay the general ones.
void Ui::drawGameControls(const GameOptionsView& v, UiActions& out) {
    std::set<std::string>& own = *v.own;
    bool mine = own.count(kGameControls) != 0;
    if (ImGui::Checkbox(tr("Botones propios de este juego"), &mine)) {
        if (mine) {
            own.insert(kGameControls);
            v.controls->copyButtons(input_.config());  // starting from the general ones
        } else {
            own.erase(kGameControls);
            if (capture_.game) capture_.active = false;
        }
        out.gameOptionsChanged = true;
    }
    if (!mine) {
        ImGui::TextDisabled("%s", tr("Usa los de Opciones > Controles."));
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Volver a los generales"))) {
        own.erase(kGameControls);
        if (capture_.game) capture_.active = false;
        out.gameOptionsChanged = true;
        return;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Deja de usar estos y vuelve a los de Opciones > Controles"));
    ImGui::TextWrapped("%s", tr("Pulsa una casilla y luego la tecla o el botón del mando. \"+\" añade otro, "
                                "\"x\" lo borra, Esc cancela."));
    if (drawBindings(*v.controls, true)) out.gameOptionsChanged = true;
}

// The options this game keeps a value of its own for, in two tabs: Ajustes and Controles.
void Ui::drawGameOptions(const UiState& state, UiActions& out) {
    ImGui::SetNextWindowSize(ImVec2(std::min(theme::px(700), ImGui::GetMainViewport()->Size.x - 20), 0), ImGuiCond_Appearing);
    if (!ImGui::Begin(tr("Opciones de este juego"), &showGameOptions_)) {
        ImGui::End();
        return;
    }
    if (!showGameOptions_ && capture_.game) capture_.active = false;
    const GameOptionsView& v = state.gameOptions;
    if (gameControls_ != v.controls && capture_.game) capture_.active = false;  // another game
    gameControls_ = v.controls;
    if (v.game.empty() || !v.values || !v.own || !v.general) {
        ImGui::TextDisabled("%s", tr("Abre un juego, o elige uno en la biblioteca con el botón derecho."));
        ImGui::End();
        return;
    }
    ImGui::TextWrapped(tr("Opciones propias de %s. Pon una opción en Propio para que este juego use su propio valor; "
                       "las que dejes en General siguen a las generales."), v.game.c_str());
    if (v.loaded && state.netPlaying) ImGui::TextDisabled("%s", tr("En una partida en red no se pueden cambiar."));
    ImGui::BeginDisabled(v.loaded && state.netPlaying);
    if (ImGui::BeginTabBar("opciones_juego")) {
        if (ImGui::BeginTabItem(tr("Ajustes"))) {
            drawGameSettings(state, out);
            ImGui::EndTabItem();
        }
        if (v.controls && ImGui::BeginTabItem(tr("Controles"), nullptr, gameControlsTab_ ? ImGuiTabItemFlags_SetSelected : 0)) {
            drawGameControls(v, out);
            ImGui::EndTabItem();
        }
        gameControlsTab_ = false;
        ImGui::EndTabBar();
    }
    ImGui::EndDisabled();
    ImGui::End();
}

}  // namespace PLUM_APP_NAMESPACE
