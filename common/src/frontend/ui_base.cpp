// The frame of the interface both programs share: fonts, events, notices, the error box, the FPS counter and the netplay window.
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
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "frontend/game_options.h"
#include "frontend/languages.h"
#include "frontend/library_names.h"
#include "frontend/theme.h"
#include "icon_png.h"
#include "logo_png.h"
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

// The interface's font: Segoe UI on Windows (it reads better than the built-in bitmap
// font and covers the accents of every language the program speaks: Latin-1 and Latin
// Extended-A, ç, ã, œ...), with the characters of other scripts that the texts shown
// have needed (a Japanese name in a game's description, say) taken from the system's
// fonts for them. Only those characters go in the atlas, so it stays small.
void Ui::buildFonts() {
    fontsDirty_ = false;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontGlyphRangesBuilder builder;
    static const ImWchar base[] = {0x0020, 0x017F, 0x2010, 0x2026, 0};
    builder.AddRanges(base);
    for (ImWchar c : fontExtra_) builder.AddChar(c);
    fontRanges_.clear();
    builder.BuildRanges(&fontRanges_);
#ifdef _WIN32
    // Japanese, Chinese, Korean and symbols, in that order: the first one with a character gives it.
    const char* const fallbacks[] = {"C:\\Windows\\Fonts\\YuGothM.ttc", "C:\\Windows\\Fonts\\meiryo.ttc",
                                     "C:\\Windows\\Fonts\\msgothic.ttc", "C:\\Windows\\Fonts\\msyh.ttc",
                                     "C:\\Windows\\Fonts\\malgun.ttf", "C:\\Windows\\Fonts\\seguisym.ttf"};
#else
    const char* const fallbacks[] = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                     "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                                     "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                                     "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"};
#endif
    const float size = std::round(18.0f * scale_);
    // The theme's two fonts, each with the other scripts merged into it.
    auto merge = [&] {
        if (fontExtra_.empty()) return;
        ImFontConfig cfg;
        cfg.MergeMode = true;
        for (const char* path : fallbacks)
            if (fileExists(path)) io.Fonts->AddFontFromFileTTF(path, size, &cfg, fontRanges_.Data);
    };
    theme::addBodyFont(size, fontRanges_.Data);
    merge();
    theme::addTitleFont(size, fontRanges_.Data);
    merge();
}

// Asks for the characters of `text` (UTF-8) that the font does not have yet; the atlas
// is made again with them before the next frame.
void Ui::needGlyphs(const std::string& text) {
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        unsigned code = c;
        int extra = 0;
        if (c >= 0xF0) { code = c & 0x07; extra = 3; }
        else if (c >= 0xE0) { code = c & 0x0F; extra = 2; }
        else if (c >= 0xC0) { code = c & 0x1F; extra = 1; }
        ++i;
        for (; extra > 0 && i < text.size(); --extra, ++i) code = (code << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
        // Beyond the base ranges and within what ImWchar holds.
        if (code <= 0x17F || (code >= 0x2010 && code <= 0x2026) || code > 0xFFFF) continue;
        if (fontExtra_.insert(static_cast<ImWchar>(code)).second) fontsDirty_ = true;
    }
}

Ui::Ui(SDL_Window* window, SDL_GLContext context, Settings& settings, Input& input)
    : window_(window), settings_(settings), input_(input) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // window layout is not worth a second settings file
    scale_ = wantedScale();
    theme::apply(scale_);
    buildFonts();
    ImGui_ImplSDL2_InitForOpenGL(window_, context);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    loadLogo();
    applyScraperSettings();
}

Ui::~Ui() {
    shelf_.reset();
    library_.clearTextures();
    gameInfo_.clearTextures();
    scraper_.clearTextures();
    manual_.clearTextures();
    if (logoTexture_) gl::DeleteTextures(1, &logoTexture_);
    if (iconTexture_) gl::DeleteTextures(1, &iconTexture_);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void Ui::loadLogo() {
    logoTexture_ = textureFromPng(kLogoPng, sizeof(kLogoPng), &logoWidth_, &logoHeight_);
    // The plum is also the window's icon (on Windows the .exe's icon already is).
    int w = 0, h = 0;
    iconTexture_ = textureFromPng(kIconPng, sizeof(kIconPng), &w, &h, window_);
}

bool Ui::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL)
        lastMouseMove_ = SDL_GetTicks();
    if (capture_.active) {
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) capture_.active = false;
            else if (capture_.column < 2) finishCapture(e.key.keysym.scancode);
            return true;
        }
        if (e.type == SDL_KEYUP || e.type == SDL_TEXTINPUT) return true;
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            if (capture_.column == 2) finishCapture(e.cbutton.button);
            return true;
        }
        if (e.type == SDL_CONTROLLERBUTTONUP) return true;
        if (e.type == SDL_CONTROLLERAXISMOTION) {
            const SDL_GameControllerButton trigger = triggerPulled(e);
            if (capture_.column == 2 && trigger != SDL_CONTROLLER_BUTTON_INVALID) finishCapture(trigger);
            return true;
        }
    }
    if (e.type == SDL_KEYDOWN || e.type == SDL_TEXTINPUT) typedOnKeys_ = true;
    else if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_FINGERDOWN) typedOnKeys_ = false;
    if (osk_.isOpen() && osk_.handleEvent(e)) return true;
    if (browser_.isOpen() && browser_.handleEvent(e)) return true;
    if (stateMenu_.open && handleStateMenu(e)) return true;
    if (showLibrary_ && handleLibrary(e)) return true;
    // With no game running and no window taking the pad, the empty screen has nothing for
    // it: A, B, Start or Back open the library again, so a pad alone never gets stuck there.
    if (!showLibrary_ && !romLoaded_ && !padNav_ && !stateMenu_.open && !browser_.isOpen() && !osk_.isOpen() &&
        e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_B:
            case SDL_CONTROLLER_BUTTON_START: case SDL_CONTROLLER_BUTTON_BACK:
                toggleLibrary();
                return true;
            default: break;
        }
    }
    ImGui_ImplSDL2_ProcessEvent(&e);
    // The pad on a window (console mode): ImGui reads it by itself, and its presses are not
    // the game's nor hotkeys, but for the states menu's.
    if (padNav_ && (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP || e.type == SDL_CONTROLLERAXISMOTION))
        return input_.hotkeyFor(e) != Hotkey::StateMenu;
    return false;
}

bool Ui::keyboardBusy() const {
    return capture_.active || stateMenu_.open || showLibrary_ || browser_.isOpen() || !error_.empty() ||
           ImGui::GetIO().WantCaptureKeyboard;
}

bool Ui::mouseBusy() const { return ImGui::GetIO().WantCaptureMouse; }

void Ui::finishCapture(int code) {
    const bool game = capture_.game && gameControls_;
    InputConfig c = game ? *gameControls_ : input_.config();
    if (capture_.column == 2) {
        auto& list = padOf(c, capture_.row);
        const auto b = static_cast<SDL_GameControllerButton>(code);
        if (!capture_.append) list.clear();
        if (std::find(list.begin(), list.end(), b) == list.end()) list.push_back(b);
    } else {
        auto& list = keysOf(c, capture_.row, capture_.column);
        const auto k = static_cast<SDL_Scancode>(code);
        if (!capture_.append) list.clear();
        if (std::find(list.begin(), list.end(), k) == list.end()) list.push_back(k);
    }
    capture_.active = false;
    if (game) {
        *gameControls_ = std::move(c);
        pendingGameOptions_ = true;
    } else {
        input_.setConfig(std::move(c));
        pendingSave_ = true;
    }
}

void Ui::setAchievements(Achievements* achievements) {
    achievements_ = achievements ? std::make_unique<AchievementsUi>(settings_, *achievements) : nullptr;
}

void Ui::notify(std::string message) {
    toast_ = std::move(message);
    toastUntil_ = SDL_GetTicks() + 2500;
}

bool Ui::consoleMode() const {
    if (consoleForced_ >= 0) return consoleForced_ == 1;
    if (settings_.consoleMode != 0) return settings_.consoleMode == 1;
    return !consoleModeReason().empty();
}

float Ui::wantedScale() const {
    if (settings_.uiScale > 0) return static_cast<float>(settings_.uiScale) / 100.0f;
    if (!consoleMode()) return 1.0f;
    // From the sofa or in the hand the letters have to be bigger: after the screen's
    // height, in quarters (1280x800 125 %, 1080p 175 %).
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    return std::clamp(std::round(static_cast<float>(h) / 640.0f * 4.0f) / 4.0f, 1.0f, 2.5f);
}

void Ui::prepareFrame() {
    const float scale = wantedScale();
    if (scale != scale_) {
        scale_ = scale;
        theme::apply(scale_);
        fontsDirty_ = true;
    }
    // In console mode the pad drives ImGui's windows (the d-pad moves, A presses, B goes
    // back, X the menu bar) while one of them has the focus; the library, the states menu
    // and the file browser drive themselves. The game waits meanwhile (padInWindows()).
    // B with nothing left to close takes the focus away, and the pad is the game's again.
    ImGuiIO& io = ImGui::GetIO();
    const bool console = consoleMode();
    io.ConfigNavEscapeClearFocusWindow = console;
    // A text box being typed in without a keyboard (with the pad, a touch or the Steam
    // Deck's trackpads): the on-screen keyboard, which takes the pad meanwhile.
    if (console && io.WantTextInput && !typedOnKeys_) osk_.open();
    else osk_.close();
    // A window counts while it has the focus and takes the pad: not the overlays nor the
    // states menu (NoNav), and the menu bar only with the pad on its menus (ImGui may hand it
    // the focus when another window closes).
    const ImGuiContext& g = *GImGui;
    const ImGuiWindow* w = g.NavWindow;
    bool focused = w && w->WasActive && !(w->RootWindow->Flags & ImGuiWindowFlags_NoNav);
    if (focused && (w->RootWindow->Flags & ImGuiWindowFlags_MenuBar) && (w->RootWindow->Flags & ImGuiWindowFlags_NoTitleBar))
        focused = g.NavLayer == ImGuiNavLayer_Menu || g.OpenPopupStack.Size > 0;
    padNav_ = console && !osk_.isOpen() && !stateMenu_.open && !showLibrary_ && !browser_.isOpen() && !capture_.active &&
              (focusMenuBar_ || focused);
    if (menuForPad_ && !padNav_ && !focusMenuBar_) {
        settings_.showMenu = false;
        menuForPad_ = false;
    }
    if (padNav_) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    else io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
}

void Ui::render() {
    theme::decorateWindows();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Ui::setSoundfonts(std::string included, std::string folder) {
    includedSoundfont_ = std::move(included);
    soundfontFolder_ = std::move(folder);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(soundfontFolder_), ec);
}

void Ui::drawError() {
    const std::string title = std::string(appInfo().name) + "##error";
    const char* const kTitle = title.c_str();
    if (!ImGui::IsPopupOpen(kTitle)) ImGui::OpenPopup(kTitle);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(error_.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    // The mouse may be hidden in full screen: Enter, Esc or the pad's A close it too.
    bool close = ImGui::Button(tr("Aceptar"), ImVec2(ImGui::GetFontSize() * 8.0f, 0));
    for (ImGuiKey key : {ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Escape, ImGuiKey_GamepadFaceDown})
        close |= ImGui::IsKeyPressed(key, false);
    if (close) {
        error_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void Ui::drawFpsCounter(const FpsCounter& fps, bool badge) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const int corner = std::clamp(settings_.fpsCorner, 0, Settings::kFpsCorners - 1);
    const bool right = corner & 1, bottom = corner & 2;
    float y = bottom ? vp->Pos.y + vp->Size.y - 10 : vp->Pos.y + static_cast<float>(menuHeight_) + 10;
    if (!bottom && right && badge) y += ImGui::GetFrameHeightWithSpacing() + 8;  // below the speed badge
    if (bottom && !right && !toast_.empty()) y -= ImGui::GetFrameHeightWithSpacing() + 8;  // above the notices
    ImGui::SetNextWindowPos(ImVec2(right ? vp->Pos.x + vp->Size.x - 10 : vp->Pos.x + 10, y), ImGuiCond_Always,
                            ImVec2(right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f));
    ImGui::SetNextWindowBgAlpha(0.65f);
    ImGui::Begin("##fps", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
    const double target = std::max(1.0, fps.target());
    const double budgetMs = 1000.0 / target;
    // Green at full speed, yellow a little below, red well below.
    auto speedColor = [&](double v) {
        return v >= target * 0.95 ? ImVec4(0.4f, 0.9f, 0.4f, 1) : v >= target * 0.75 ? ImVec4(1.0f, 0.85f, 0.3f, 1)
                                                                                     : ImVec4(1.0f, 0.4f, 0.35f, 1);
    };
    const ImVec4 emuColor(1.0f, 0.6f, 0.2f, 1), workColor(0.45f, 0.7f, 1.0f, 1);
    ImGui::TextColored(speedColor(fps.fps()), "%.1f FPS", fps.fps());
    ImGui::SameLine();
    ImGui::TextDisabled("/ %.0f   %s %.0f", target, tr("pantalla"), fps.screenFps());
    ImGui::TextColored(emuColor, "%s %.2f ms", tr("Emulación"), fps.emuMs());
    ImGui::SameLine();
    ImGui::TextDisabled("(%.0f %%)", fps.emuMs() / budgetMs * 100.0);
    ImGui::SameLine();
    ImGui::TextColored(workColor, "%s %.2f ms", tr("Total"), fps.workMs());

    // A graph of the last seconds: time on X (now on the right), the value on Y, with a
    // dashed line at the target (the console's rate, or the time one frame may take).
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float labelW = ImGui::CalcTextSize("0000").x + 4;
    auto graph = [&](const char* id, double yMax, double line, auto value, std::initializer_list<ImVec4> colors,
                     const char* unit) {
        const ImVec2 size(ImGui::GetFontSize() * 15.0f, ImGui::GetFontSize() * 3.6f);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(size.x + labelW, size.y));
        ImGui::PushID(id);
        const ImVec2 a(at.x + labelW, at.y), b(a.x + size.x, a.y + size.y);
        draw->AddRectFilled(a, b, IM_COL32(0, 0, 0, 110));
        const ImU32 axis = IM_COL32(200, 200, 200, 140);
        draw->AddLine(a, ImVec2(a.x, b.y), axis);
        draw->AddLine(ImVec2(a.x, b.y), b, axis);
        auto yOf = [&](double v) { return b.y - static_cast<float>(std::clamp(v / yMax, 0.0, 1.0)) * size.y; };
        char text[16];
        std::snprintf(text, sizeof text, "%.0f", yMax);
        draw->AddText(ImVec2(at.x, a.y - 2), axis, text);
        draw->AddText(ImVec2(at.x, b.y - ImGui::GetFontSize() + 2), axis, "0");
        draw->AddText(ImVec2(at.x, (a.y + b.y - ImGui::GetFontSize()) * 0.5f), axis, unit);
        const float ly = yOf(line);
        for (float x = a.x; x < b.x; x += 8) draw->AddLine(ImVec2(x, ly), ImVec2(std::min(x + 4, b.x), ly), IM_COL32(255, 255, 255, 120));
        const int n = fps.count();
        const float dx = size.x / static_cast<float>(FpsCounter::kPoints - 1);
        int series = 0;
        for (const ImVec4& c : colors) {
            ImVec2 prev;
            for (int i = 0; i < n; ++i) {
                const ImVec2 p(b.x - static_cast<float>(n - 1 - i) * dx, yOf(value(fps.point(i), series)));
                if (i) draw->AddLine(prev, p, ImGui::ColorConvertFloat4ToU32(c), 1.5f);
                prev = p;
            }
            ++series;
        }
        ImGui::PopID();
    };
    // The scales grow with what was measured, but not so much that a single hiccup (a
    // game being loaded) flattens the rest: what goes past the top is cut.
    double fpsMax = target * 1.25, msMax = budgetMs * 1.5;
    for (int i = 0; i < fps.count(); ++i) {
        fpsMax = std::max(fpsMax, static_cast<double>(fps.point(i).fps) * 1.1);
        msMax = std::max(msMax, static_cast<double>(std::max(fps.point(i).emuMs, fps.point(i).workMs)) * 1.1);
    }
    fpsMax = std::min(fpsMax, target * 4);
    msMax = std::min(msMax, budgetMs * 4);
    graph("fps", std::ceil(fpsMax / 10) * 10, target, [](const FpsCounter::Point& p, int) { return p.fps; },
          {speedColor(fps.fps())}, "FPS");
    graph("ms", std::ceil(msMax / 5) * 5, budgetMs,
          [](const FpsCounter::Point& p, int s) { return s == 0 ? p.workMs : p.emuMs; }, {workColor, emuColor}, "ms");
    ImGui::TextDisabled("%s", tr("-10 s  ·  ahora a la derecha"));
    ImGui::End();
}

void Ui::drawNetplay(const UiState& state, UiActions& out) {
    if (!ImGui::Begin(tr("Jugar en red"), &showNetplay_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
    if (ImGui::IsWindowAppearing()) {
        localAddresses_ = UdpTransport::localAddresses();
        std::snprintf(netAddress_, sizeof netAddress_, "%s", settings_.netAddress.c_str());
    }
    if (state.netSession) {
        ImGui::TextWrapped("%s", state.netStatus.c_str());
        if (state.netProgress >= 0) ImGui::ProgressBar(state.netProgress, ImVec2(ImGui::GetFontSize() * 36.0f, 0));
        ImGui::Spacing();
        if (ImGui::Button(tr("Salir de la partida"))) out.netLeave = true;
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("%s", tr("Dos jugadores, cada uno en su ordenador y con el mismo juego abierto (la misma ROM). "
                       "Quien aloja es el jugador 1 y quien se une, el jugador 2; cada uno juega con sus "
                       "controles del jugador 1. La partida empieza donde la tenga el anfitrión."));
    if (!state.romLoaded)
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", tr("Abre primero el juego."));
    ImGui::BeginDisabled(!state.romLoaded);

    ImGui::SeparatorText(tr("Alojar la partida (jugador 1)"));
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt(tr("Puerto UDP"), &settings_.netPort, 0, 0)) settings_.netPort = std::clamp(settings_.netPort, 1, 65535);
    if (ImGui::IsItemDeactivatedAfterEdit()) out.settingsChanged = true;
    ImGui::SameLine();
    if (ImGui::Button(tr("Alojar"))) out.netHost = out.settingsChanged = true;
    if (!localAddresses_.empty())
        ImGui::TextWrapped(tr("En casa, tu amigo se une a %s:%d."), localAddresses_.front().c_str(), settings_.netPort);
    ImGui::TextDisabled(tr("Por internet: abre (redirige) el puerto UDP %d de tu router hacia este ordenador y "
                        "dale tu IP pública; o usad los dos una red privada como Tailscale o ZeroTier "
                        "y no hace falta abrir nada."), settings_.netPort);

    ImGui::SeparatorText(tr("Unirse a una partida (jugador 2)"));
    ImGui::SetNextItemWidth(300);
    const bool enter = ImGui::InputTextWithHint(tr("Anfitrión"), tr("IP o nombre, y :puerto si no es el de arriba"),
                                                netAddress_, sizeof netAddress_, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button(tr("Unirse")) || enter) && netAddress_[0]) {
        settings_.netAddress = netAddress_;
        out.netJoin = out.settingsChanged = true;
    }

    ImGui::SeparatorText(tr("Ajustes"));
    ImGui::SetNextItemWidth(160);
    ImGui::SliderInt(tr("Retardo de los mandos"), &settings_.netInputDelay, 0, 6, tr("%d fotogramas"), ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivatedAfterEdit()) out.settingsChanged = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("Cuántos fotogramas tarda en hacer efecto lo que pulsas. Con más retardo hay menos\n"
                          "saltos cuando la conexión es lenta; con 0 responde al instante pero salta más.\n"
                          "Vale el del anfitrión. 2 va bien casi siempre."));
    ImGui::EndDisabled();
    ImGui::PopTextWrapPos();
    ImGui::End();
}

}  // namespace PLUM_APP_NAMESPACE
