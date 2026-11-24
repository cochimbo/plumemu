// The library's games: which are shown, its tabs and favourites, its folders and its list view.
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

void Ui::rescanLibrary() {
    library_.scan(settings_.libraryFolders);
    libraryView_.selected = 0;
    coversFirstFor_ = -1;
    libraryView_.scroll = true;
}

std::vector<size_t> Ui::visibleGames() {
    const std::string filter = searchKey(libraryView_.search);
    std::vector<size_t> out;
    const auto& games = library_.games();
    auto matches = [&](size_t i) { return filter.empty() || games[i].key.find(filter) != std::string::npos; };
    // The history goes the last game played first, whatever the order chosen; only the
    // games in the library's folders.
    if (settings_.librarySystem == Settings::kTabHistory) {
        std::map<std::string, size_t> byPath;
        for (size_t i = 0; i < games.size(); ++i) byPath.emplace(Settings::pathKey(games[i].path), i);
        for (const auto& h : settings_.history)
            if (const auto it = byPath.find(Settings::pathKey(h)); it != byPath.end() && matches(it->second))
                out.push_back(it->second);
        return out;
    }
    const bool favorites = settings_.librarySystem == Settings::kTabFavorites;
    for (size_t i = 0; i < games.size(); ++i) {
        if (settings_.librarySystem >= 0 && static_cast<int>(games[i].system) != settings_.librarySystem) continue;
        if (favorites && !isFavorite(games[i])) continue;
        if (matches(i)) out.push_back(i);
    }
    // The library is in title order; the other orders keep it among equals, and games
    // with nothing to sort by go last.
    // The 3D shelf keeps each console's games together (in the consoles' own order) and
    // each console's in title order, whatever the order chosen for the other views.
    if (settings_.libraryBoxes) {
        std::stable_sort(out.begin(), out.end(), [&games](size_t a, size_t b) {
            return static_cast<int>(games[a].system) < static_cast<int>(games[b].system);
        });
        return out;
    }
    const int sort = settings_.librarySort;
    const bool descending = libraryView_.descending;
    if (sort == 0) {
        if (descending) std::reverse(out.begin(), out.end());
        return out;
    }
    std::vector<std::pair<std::string, size_t>> keys;
    keys.reserve(out.size());
    for (size_t i : out) {
        const Library::Game& g = games[i];
        std::string key;
        if (sort == 1) {
            key = systemName(g.system);
        } else if (const GameDetails* d = gameInfo_.details(g.system, g.stem)) {
            if (sort == 2) key = d->year;
            else if (sort == 3) key = d->genre.empty() ? std::string() : searchKey(genreName(d->genre) ? tr(genreName(d->genre)) : d->genre);
            else key = searchKey(d->developer);
        }
        keys.emplace_back(std::move(key), i);
    }
    std::stable_sort(keys.begin(), keys.end(), [descending](const auto& a, const auto& b) {
        if (a.first.empty() != b.first.empty()) return b.first.empty();
        if (a.first != b.first) return descending ? a.first > b.first : a.first < b.first;
        return a.second < b.second;
    });
    for (size_t n = 0; n < keys.size(); ++n) out[n] = keys[n].second;
    return out;
}

void Ui::setLibraryView(int view) {
    if ((settings_.libraryBoxes ? 2 : settings_.libraryList ? 1 : 0) == view) return;
    settings_.libraryList = view == 1;
    settings_.libraryBoxes = view == 2;
    if (shelf_) shelf_->reset();
    libraryView_.syncSort = true;
    libraryView_.scroll = true;
    pendingSave_ = true;
}

// While the library is open it owns the pad and the keyboard (except while typing in
// the search box): the arrows move through the grid, A (or Enter) plays the game, B
// goes back to the game if one is running (Esc closes it always), Y (or Ctrl+F) goes to the search
// box, Back changes the view, and the shoulder buttons (or Page Up and Page Down) change
// the console tab.
bool Ui::isFavorite(const Library::Game& g) {
    // Looked at again once a frame: the favourites only change between frames.
    if (favoriteFrame_ != ImGui::GetFrameCount() && (favoriteFrame_ = ImGui::GetFrameCount(), favoriteKeysFor_ != settings_.favorites)) {
        favoriteKeysFor_ = settings_.favorites;
        favoriteKeys_.clear();
        for (const auto& f : settings_.favorites) favoriteKeys_.insert(Settings::pathKey(f));
    }
    return !favoriteKeys_.empty() && favoriteKeys_.count(Settings::pathKey(g.path)) > 0;
}

void Ui::setLibraryTab(int tab) {
    if (settings_.librarySystem == tab) return;
    settings_.librarySystem = tab;
    libraryView_.selected = 0;
    libraryView_.scroll = true;
    pendingSave_ = true;
    if (shelf_) shelf_->reset();
}

// The library's folders: each with a button to take it out, and one to add another.
void Ui::drawLibraryFolders(UiActions& out) {
    int remove = -1;
    for (int i = 0; i < static_cast<int>(settings_.libraryFolders.size()); ++i) {
        ImGui::PushID(i);
        if (ImGui::SmallButton(tr("Quitar"))) remove = i;
        ImGui::SameLine();
        ImGui::TextUnformatted(settings_.libraryFolders[static_cast<size_t>(i)].c_str());
        ImGui::PopID();
    }
    if (remove >= 0) {
        settings_.libraryFolders.erase(settings_.libraryFolders.begin() + remove);
        out.settingsChanged = true;
        rescanLibrary();
    }
    if (ImGui::Button(tr("Añadir carpeta..."))) {
        browseFor_ = BrowseFor::LibraryFolder;
        browser_.open(settings_.libraryFolders.empty() ? settings_.lastFolder : settings_.libraryFolders.back(), Pick::Folder);
    }
}

// What clicking a game does, in the grid and in the list: a click picks it, a double
// click plays it, and the right button opens its menu.
void Ui::libraryItem(bool clicked, int n) {
    if (clicked) {
        libraryView_.selected = n;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) libraryView_.load = n;
    }
    // On release, like ImGui's own context menus: the press closes open popups.
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        libraryView_.selected = n;
        libraryView_.menuGame = n;
        libraryView_.menuItem = 0;
        libraryView_.openMenu = true;
    }
}

// The games as a table: title, console, year, genre, developer and players, sorted by
// clicking a column's header (or with "Ordenar por").
void Ui::drawLibraryList(const std::vector<size_t>& visible) {
    const auto& games = library_.games();
    libraryView_.columns = 1;
    const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("lista", 6, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(tr("Título"), ImGuiTableColumnFlags_DefaultSort, 3.0f, 0);
    ImGui::TableSetupColumn(tr("Consola"), 0, 1.0f, 1);
    ImGui::TableSetupColumn(tr("Año"), ImGuiTableColumnFlags_PreferSortDescending, 0.5f, 2);
    ImGui::TableSetupColumn(tr("Género"), 0, 1.1f, 3);
    ImGui::TableSetupColumn(tr("Desarrolladora"), 0, 1.4f, 4);
    ImGui::TableSetupColumn(tr("Jugadores"), ImGuiTableColumnFlags_NoSort, 0.6f, 5);
    // The order chosen elsewhere (the settings, "Ordenar por") shown on the headers;
    // the columns are in the same order as the sorts.
    if (libraryView_.syncSort) {
        ImGui::TableSetColumnSortDirection(settings_.librarySort,
                                           libraryView_.descending ? ImGuiSortDirection_Descending : ImGuiSortDirection_Ascending,
                                           false);
        libraryView_.syncSort = false;
    }
    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
        if (specs->SpecsCount > 0) {
            const int sort = static_cast<int>(specs->Specs[0].ColumnUserID);
            const bool descending = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            if (sort != settings_.librarySort || descending != libraryView_.descending) {
                settings_.librarySort = sort;
                libraryView_.descending = descending;
                pendingSave_ = true;
            }
        }
        specs->SpecsDirty = false;
    }
    ImGui::TableHeadersRow();
    const std::map<std::string, int>& withCheats = gamesWithCheats();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    if (libraryView_.scroll && libraryView_.selected < static_cast<int>(visible.size()))
        clipper.IncludeItemByIndex(libraryView_.selected);
    while (clipper.Step()) {
        for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
            const Library::Game& g = games[visible[static_cast<size_t>(n)]];
            const GameDetails* d = gameInfo_.details(g.system, g.stem);
            const bool selected = n == libraryView_.selected;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(n);
            libraryItem(ImGui::Selectable(g.title.c_str(), selected,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick),
                        n);
            const auto cheats = withCheats.find(g.stem);
            if (ImGui::IsItemHovered() && (!g.tags.empty() || cheats != withCheats.end())) {
                std::string tip = g.tags.empty() ? g.path : g.tags + "\n" + g.path;
                if (cheats != withCheats.end())
                    tip += "\n" + trf(cheats->second == 1 ? N_("%d truco activo") : N_("%d trucos activos"), cheats->second);
                ImGui::SetTooltip("%s", tip.c_str());
            }
            // After the title, a star (a favourite) and a bolt (cheats on).
            {
                const float s = ImGui::GetTextLineHeight();
                ImVec2 at(ImGui::GetItemRectMin().x + ImGui::CalcTextSize(g.title.c_str()).x + 6,
                          ImGui::GetItemRectMin().y + (ImGui::GetItemRectSize().y - s) * 0.5f);
                if (isFavorite(g)) {
                    drawStar(ImGui::GetWindowDrawList(), at, s, kFavoriteColour);
                    at.x += s + 4;
                }
                if (cheats != withCheats.end()) drawCheatIcon(ImGui::GetWindowDrawList(), at, s, kCheatColour);
            }
            if (selected && libraryView_.scroll) {
                ImGui::SetScrollHereY(0.5f);
                libraryView_.scroll = false;
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(systemName(g.system));
            if (d) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(d->year.c_str());
                ImGui::TableNextColumn();
                const char* genre = genreName(d->genre);
                ImGui::TextUnformatted(genre ? tr(genre) : d->genre.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(d->developer.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(d->players.c_str());
            }
        }
    }
    ImGui::EndTable();
}

}  // namespace PLUM_APP_NAMESPACE
