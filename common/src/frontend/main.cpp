// The window: its main loop, the same for both programs. Each builds it in its namespace
// with its own frontend/app_main.h, whose AppMain does what only that program does (plumsg's
// Roland sound, instrument editor and light guns; plumnin's synthesizer and firmware).
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/netplay.h"
#include "core/recording.h"
#include "core/rewind.h"
#include "frontend/app_main.h"
#include "frontend/audio.h"
#include "frontend/fps_counter.h"
#include "frontend/handheld.h"
#include "frontend/http.h"
#include "frontend/languages.h"
#include "miniz.h"
#include "net/udp.h"
#include "render/profile3d.h"

using namespace PLUM_APP_NAMESPACE;

namespace {

// Saves what is on screen (the whole window) as a PNG, for screenshots taken from the
// command line.
bool captureWindow(SDL_Window* window, const std::string& path) {
    int w, h;
    SDL_GL_GetDrawableSize(window, &w, &h);
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4), flipped(pixels.size());
    gl::ReadPixels(0, 0, w, h, gl::RGBA, gl::UNSIGNED_BYTE, pixels.data());
    const size_t row = static_cast<size_t>(w) * 4;
    for (int y = 0; y < h; ++y)
        std::copy_n(&pixels[static_cast<size_t>(h - 1 - y) * row], row, &flipped[static_cast<size_t>(y) * row]);
    size_t size = 0;
    void* png = tdefl_write_image_to_png_file_in_memory(flipped.data(), w, h, 4, &size);
    if (!png) return false;
    std::vector<uint8_t> bytes(static_cast<uint8_t*>(png), static_cast<uint8_t*>(png) + size);
    mz_free(png);
    return writeFile(path, bytes);
}

// Width of the picture at 224 lines: 4:3, plus the widescreen columns.
int pictureWidth(const Settings& s) { return 299 * (kConsoleWidth + s.wideLeft + s.wideRight) / kConsoleWidth; }

// Window size for a scale factor: the picture at 224 lines per scale step, plus the menu bar.
void applyWindowScale(SDL_Window* window, int scale, int menuHeight, const Settings& s) {
    SDL_SetWindowSize(window, pictureWidth(s) * scale, 224 * scale + menuHeight);
}

struct CommandLine {
    std::string rom;
    std::string capture;  // --screenshot file.png: take a screenshot and quit
    int frames = 120;     // --frames N: frames to run before the screenshot
    int pseudo3d = -1;    // --3d / --2d override the setting
    std::string view;     // --view name
    float tilt = -1000, yaw = -1000;
    bool stateMenu = false;  // --states-menu: start with the states menu open
    int stateTab = -1;       // --states-tab N: on that tab (0 the slots ... 5 Sistema)
    bool search = false;     // --search: with --library, typing in its search box (screenshots)
    int windowW = 0, windowH = 0;  // --window-size WxH: the window's size (a Steam Deck's 1280x800)
    std::vector<std::string> library;  // --library folder (repeatable): start with the library of those folders open
    int filter = -1;         // --filter none|scanlines|crt (|lcd where there is one)
    int upscale = -1;        // --upscaling none|xbr|anime4k
    bool fps = false;        // --fps: with the FPS counter on
    bool libraryList = false;     // --list: with --library, show it as a list
    int libraryBoxes = -1;        // --boxes [1-5]: with --library, show its 3D boxes (and hold one open at that step)
    int media = 0;                // --media [search|all]: with --library, the first game's ScreenScraper media (1 downloaded, 2 searched for, 3 every game's)
    bool cheats = false;          // --cheats: start with the game's Cheats window open
    bool achievements = false;    // --achievements: start with the RetroAchievements window open
    int gameOptions = 0;          // --game-options [controls]: start with its Options for this game window open (2 on Controls)
    AppCommandLine app;           // the program's own
    // For launchers (LaunchBox, Playnite, frontends of arcade cabinets). These last only
    // for this session: the settings file keeps what it had.
    int fullscreen = -1;          // --fullscreen / --windowed
    int console = -1;             // --console-mode / --desktop-mode: console mode on or off
    int region = -1;              // --region auto|jp|us|eu (a Region)
    int slot = -1;                // --load-state N: load the game's state N once it is open
    bool netHost = false;         // --host: host a netplay game with the open game
    std::string netJoin;          // --join address: join that host
    std::string record;           // --record file.avi|file.gif: record the game from the start
    bool help = false;            // --help
    std::vector<std::string> unknown;  // options the program does not know
};

// The Spanish names options had up to plumsg 0.0.31, still accepted (by the programs that
// had them) so launchers set up with them keep working.
std::string optionName(const std::string& a) {
    static const std::pair<const char*, const char*> kSpanish[] = {
        {"--captura", "--screenshot"}, {"--grabar", "--record"}, {"--cuadros", "--frames"}, {"--vista", "--view"},
        {"--inclinacion", "--tilt"}, {"--giro", "--yaw"}, {"--menu-estados", "--states-menu"},
        {"--biblioteca", "--library"}, {"--lista", "--list"},
        {"--medios", "--media"}, {"--trucos", "--cheats"}, {"--logros", "--achievements"},
        {"--opciones-juego", "--game-options"}, {"--instrumentos", "--instruments"}, {"--pista", "--track"}, {"--partitura", "--score"},
        {"--filtro", "--filter"}, {"--reescalado", "--upscaling"}, {"--pantalla-completa", "--fullscreen"},
        {"--modo-consola", "--console-mode"}, {"--modo-escritorio", "--desktop-mode"},
        {"--ventana", "--windowed"}, {"--estado", "--load-state"}, {"--alojar", "--host"}, {"--unirse", "--join"},
        {"--ayuda", "--help"}};
    if (!kSpanishOptions) return a;
    for (const auto& [es, en] : kSpanish)
        if (a == es) return en;
    return a;
}

CommandLine parse(int argc, char** argv) {
    CommandLine c;
    // A value, or one of the Spanish ones it had too.
    auto is = [](const std::string& v, const char* en, const char* es) { return v == en || (kSpanishOptions && v == es); };
    for (int i = 1; i < argc; ++i) {
        const std::string a = optionName(argv[i]);
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--screenshot") c.capture = next();
        else if (a == "--record") c.record = next();
        else if (a == "--frames") c.frames = std::max(1, std::atoi(next().c_str()));
        else if (a == "--3d") c.pseudo3d = 1;
        else if (a == "--2d") c.pseudo3d = 0;
        else if (a == "--view") c.view = next();
        else if (a == "--tilt") c.tilt = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--yaw") c.yaw = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--states-menu") c.stateMenu = true;
        else if (a == "--search") c.search = true;
        else if (a == "--states-tab") {
            c.stateMenu = true;
            c.stateTab = std::atoi(next().c_str());
        } else if (a == "--window-size") {
            const std::string v = next();
            const size_t x = v.find('x');
            if (x != std::string::npos) {
                c.windowW = std::atoi(v.substr(0, x).c_str());
                c.windowH = std::atoi(v.substr(x + 1).c_str());
            }
        }
        else if (a == "--fps") c.fps = true;
        else if (a == "--library") c.library.push_back(next());
        else if (a == "--list") c.libraryList = true;
        else if (a == "--boxes") {
            // Undocumented steps, for screenshots: 1 the box out, 2 its cartridge out, 3 on the way, 4 in the console, 5 its manual read.
            c.libraryBoxes = 0;
            if (i + 1 < argc && argv[i + 1][0] >= '1' && argv[i + 1][0] <= '5' && !argv[i + 1][1]) c.libraryBoxes = argv[++i][0] - '0';
        }
        else if (a == "--media") {
            c.media = 1;
            const std::string mode = i + 1 < argc ? argv[i + 1] : "";
            if (is(mode, "search", "buscar")) c.media = 2, ++i;
            else if (is(mode, "all", "todos")) c.media = 3, ++i;
        }
        else if (a == "--cheats") c.cheats = true;
        else if (a == "--achievements") c.achievements = true;
        else if (a == "--game-options") {
            c.gameOptions = 1;
            if (i + 1 < argc && is(argv[i + 1], "controls", "controles")) {
                c.gameOptions = 2;
                ++i;
            }
        }
        else if (c.app.parse(a, i, argc, argv)) {}
        else if (a == "--filter") {
            const std::string f = next();
            c.filter = kLcdFilter && f == "lcd" ? 3 : f == "crt" ? 2 : f == "scanlines" ? 1 : 0;
        }
        else if (a == "--upscaling") {
            const std::string r = next();
            c.upscale = r == "anime4k" ? 2 : r == "xbr" ? 1 : 0;
        }
        else if (a == "--fullscreen") c.fullscreen = 1;
        else if (a == "--windowed") c.fullscreen = 0;
        else if (a == "--console-mode") c.console = 1;
        else if (a == "--desktop-mode") c.console = 0;
        else if (a == "--region") {
            const std::string r = next();
            if (r == "auto") c.region = static_cast<int>(Region::Auto);
            else if (r == "jp" || is(r, "japan", "japon")) c.region = static_cast<int>(Region::JapanNtsc);
            else if (r == "us" || r == "usa") c.region = static_cast<int>(Region::UsaNtsc);
            else if (r == "eu" || is(r, "europe", "europa") || r == "pal") c.region = static_cast<int>(Region::EuropePal);
            else c.unknown.push_back(a + " " + r);
        } else if (a == "--load-state") {
            const std::string n = next();
            char* end = nullptr;
            const long slot = std::strtol(n.c_str(), &end, 10);
            if (!n.empty() && *end == 0 && slot >= 0 && slot < kSlots) c.slot = static_cast<int>(slot);
            else c.unknown.push_back(a + " " + n);
        } else if (a == "--host") c.netHost = true;
        else if (a == "--join") {
            c.netJoin = next();
            if (c.netJoin.empty()) c.unknown.push_back(a);
        } else if (a == "--help" || a == "-h" || a == "/?") c.help = true;
        else if (a.size() > 1 && a[0] == '-') c.unknown.push_back(a);
        else c.rom = a;
    }
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    const CommandLine cmd = parse(argc, argv);
    const Paths paths = findPaths();
    const std::string settingsDir = paths.ini.substr(0, paths.ini.find_last_of("/\\") + 1);
    AppMain::prepareFolders(paths, settingsDir);
    // Next to the .ini: the reports of the runs that ended in a crash.
    const std::string crashDir = settingsDir + "crash-reports/";
    installCrashHandler(crashDir);
    Settings settings = Settings::load(paths.ini);
    AppMain::settingsLoaded(paths);
    // Until a language is picked in Opciones > Idioma, the system's.
    applyLanguage(settings.language.empty() ? systemLanguage() : settings.language);
    if (cmd.help || !cmd.unknown.empty()) {
        // A window build on Windows has no console, so this goes to a message box too.
        std::string text = tr(kHelp);
        if (!cmd.unknown.empty()) {
            text = trf("Opción desconocida: %s", cmd.unknown.front().c_str()) + "\n\n" + text;
        }
        std::fprintf(cmd.help ? stdout : stderr, "%s\n", text.c_str());
        SDL_ShowSimpleMessageBox(cmd.help ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_ERROR, appInfo().name, text.c_str(),
                                 nullptr);
        return cmd.help ? 0 : 2;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const bool capturing = !cmd.capture.empty();
    auto input = std::make_unique<Input>(InputConfig::loadOrCreate(paths.ini));
    // A screenshot run never touches the user's settings.
    auto applyCommandLine = [&] {
        if (cmd.pseudo3d >= 0) settings.render.pseudo3d = cmd.pseudo3d == 1;
        if (!cmd.view.empty()) settings.view = cmd.view;
        if (cmd.tilt > -1000) settings.render.tilt = cmd.tilt;
        if (cmd.yaw > -1000) settings.render.yaw = cmd.yaw;
        if (cmd.filter >= 0) settings.render.filter = static_cast<Filter2D>(cmd.filter);
        if (cmd.upscale >= 0) settings.render.upscale = static_cast<Upscale2D>(cmd.upscale);
        if (cmd.fps) settings.fpsCounter = true;
    };
    applyCommandLine();
    // Fullscreen and region from the command line are for this session only.
    const bool fileFullscreen = settings.fullscreen;
    const Region fileRegion = settings.region;
    if (cmd.fullscreen >= 0) settings.fullscreen = cmd.fullscreen == 1;
    if (cmd.region >= 0) settings.region = static_cast<Region>(cmd.region);
    // Console mode (a Steam Deck, a ROG Ally, Big Picture...) starts in full screen without
    // the menu bar, for this session: the settings file keeps the desktop's.
    const bool consoleStart = (!capturing || cmd.console == 1) && (cmd.console >= 0 ? cmd.console == 1
                                             : settings.consoleMode ? settings.consoleMode == 1
                                                                    : !consoleModeReason().empty());
    const bool fileShowMenu = settings.showMenu;
    if (consoleStart) {
        if (cmd.fullscreen < 0) settings.fullscreen = true;
        settings.showMenu = false;
    }
    auto toSave = [&](Settings s) {
        if (consoleStart) {
            s.fullscreen = fileFullscreen;
            s.showMenu = fileShowMenu;
        }
        if (cmd.fullscreen >= 0) s.fullscreen = fileFullscreen;
        if (cmd.region >= 0) s.region = fileRegion;
        return s;
    };
    if (capturing) settings.fullscreen = false;
    // The general values of the options a game can have its own of. While a game with
    // some of its own is loaded, settings holds the game's and the general ones wait here.
    Settings general = toSave(settings);
    GameOverrides game{{}, general, input->config()};  // the loaded game's own options

    // OpenGL 3.3 core everywhere; macOS only hands out core contexts marked forward compatible.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* window = SDL_CreateWindow(appTitle().c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          cmd.windowW > 0 ? cmd.windowW : pictureWidth(settings) * settings.windowScale,
                                          cmd.windowH > 0 ? cmd.windowH : 224 * settings.windowScale,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 320, 224);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    const char* missing = context ? gl::load() : nullptr;
    if (!context || missing) {
        const std::string message =
            trf("%s necesita una tarjeta gráfica con OpenGL 3.3 y sus drivers actualizados.", appInfo().name) + "\n\n" +
            std::string(context ? trf("Falta la función %s", missing) : SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, appInfo().name, message.c_str(), window);
        return 1;
    }
    SDL_GL_SetSwapInterval(0);  // the loop below paces itself to the console's refresh rate

    std::vector<std::unique_ptr<Renderer>> renderers;
    renderers.push_back(std::make_unique<FlatRenderer>());
    renderers.push_back(std::make_unique<LayerRenderer>());
    auto pseudo3d = std::make_unique<Pseudo3DRenderer>();
    UiState uiState;
    for (const auto& r : renderers) uiState.views.push_back(r->name());
    auto viewIndex = [&] {
        for (size_t i = 0; i < renderers.size(); ++i)
            if (settings.view == renderers[i]->name()) return i;
        return size_t{0};
    };
    // An unknown view in the settings file (an old name, a typo) falls back to 2D.
    settings.view = renderers[viewIndex()]->name();

    input->openAll();
    auto ui = std::make_unique<Ui>(window, context, settings, *input);
    if (cmd.console >= 0) ui->forceConsoleMode(cmd.console == 1);
    // The last run crashed: say where its report is, so that it can be sent.
    if (const std::string report = takeCrashReport(crashDir); !report.empty() && !capturing)
        ui->showError(trf("%s se cerró por un fallo la última vez.\nEl informe está en %s", appInfo().name, report.c_str()));
    AppMain extras;  // before the console, which may point at its sound
    auto app = std::make_unique<App>();
    newConsole(app->console);
    app->statesDir = paths.states;
    app->profilesDir = paths.profiles;
    app->cheatsDir = paths.cheats;
    ui->library().setCoversFolder(paths.covers);
    // The covers as decoded, so that the next start does not decode them again: in
    // cache/covers beside the covers folder.
    ui->library().setCacheFolder(
        (std::filesystem::u8path(paths.covers).parent_path().parent_path() / "cache" / "covers").u8string() +
        static_cast<char>(std::filesystem::path::preferred_separator));
    ui->gameInfo().setFolder(paths.info);
    ui->scraper().setFolder(paths.info + "screenscraper/");
    ui->library().setMediaFolder(paths.info + "screenscraper/");
    ui->gameInfo().setMediaFolder(paths.info + "screenscraper/");
    auto audio = std::make_unique<Audio>();
    // RetroAchievements: a screenshot run shows its windows but never logs in.
    Achievements achievements([](const std::string& url, const std::string& post, const std::string& agent,
                                 std::string& body) {
        std::vector<uint8_t> data;
        const int status = httpRequest(url, post, agent, data);
        body.assign(data.begin(), data.end());
        return status;
    });
    ui->setAchievements(&achievements);
    auto hardcore = [&] { return !capturing && settings.achievements && settings.raHardcore; };
    app->noCheats = hardcore();
    // Logs in (with the token kept from the last time) and identifies the loaded game,
    // or logs out when switched off.
    auto applyAchievements = [&] {
        if (capturing) return;
        achievements.setHardcore(hardcore());
        if (!settings.achievements) {
            if (achievements.loggedIn() || achievements.loggingIn()) achievements.logout();
            return;
        }
        if (!achievements.loggedIn() && !achievements.loggingIn() && !settings.raToken.empty())
            achievements.loginWithToken(settings.raUser, settings.raToken);
        if (app->romLoaded && !achievements.gameLoaded() && !achievements.loadingGame())
            achievements.loadGame(*app->console, app->achievementsHash);
    };
    applyAchievements();

    auto applyConsole = [&] {
        if (!app->console) return;
        app->console->setRegion(settings.region);
        app->console->setPadType(0, settings.padType[0]);
        app->console->setPadType(1, settings.padType[1]);
        extras.applyConsole();
    };
    // Netplay. The session talks through the socket, so it goes first on the way out.
    std::unique_ptr<UdpTransport> netSocket;
    std::unique_ptr<NetplaySession> net;
    NetplaySession::Phase netPhase = NetplaySession::Phase::Ended;
    // Connected (or connecting): what would make the two consoles differ is off.
    auto netPlaying = [&] { return net && net->phase() != NetplaySession::Phase::Waiting; };
    MainContext context_{paths, settings, *ui, *input, *app, window, capturing, hardcore, netPlaying, applyConsole};
    extras.setup(context_);
    applyConsole();
    ui->setCheatsFolder(paths.cheats);
    ui->setModelFolders(paths.myModels == paths.models ? std::vector<std::string>{paths.models}
                                                       : std::vector<std::string>{paths.myModels, paths.models});
    extras.started();
    // Each option goes to the game's file if the game has its own value of it, else to the .ini.
    auto save = [&] {
        if (capturing) return;
        Settings out = toSave(settings);
        for (const GameOption& o : gameOptions()) {
            if (game.has(o.key)) o.copy(general, out);
            else o.copy(settings, general);
        }
        out.save(paths.ini, input->config());
        if (!app->romLoaded) return;
        game.values = settings;
        app->writeOverrides(app->romPath, game);
    };
    RewindBuffer rewind;
    bool runAheadFailed = false;  // the console could not go back once: no more for this game
    FramePacer pacer;
    auto setRewindLimits = [&] {
        rewind.setLimits(static_cast<size_t>(settings.rewindSeconds * app->refreshRate()), kRewindBytes);
    };
    setRewindLimits();
    // A game takes its own options where it has them, and the general ones elsewhere.
    // Its own buttons go over the general ones in Input, which keeps those for the .ini.
    auto applyControls = [&] { input->setGameButtons(game.has(kGameControls) ? &game.controls : nullptr); };
    auto useGameOptions = [&](const std::string& path) {
        game.restore(general, settings);
        game = app->readOverrides(path, general, input->config());
        game.applyTo(settings);
        applyControls();
        applyConsole();
        extras.gameOptionsUsed(path);
    };
    // Grabar vídeo / Grabar GIF: the game as it is played, in the recordings folder.
    GameRecording video;
    std::vector<uint32_t> screen;  // the game's part of the window, read back for it
    // The 2D view records the console's own picture, sharp and without the filter; the
    // other views what is on screen, the only place their picture exists.
    auto recordsConsole = [&] { return !settings.render.pseudo3d && viewIndex() == 0; };
    auto stopVideo = [&] {
        if (!video.active()) return;
        const bool gif = video.format() == GameRecording::Format::Gif;
        std::string error;
        if (!video.finish(&error)) ui->showError(error);
        else if (gif) ui->notify(trf("GIF guardado en %s", video.path().c_str()));
        else ui->notify(trf("Vídeo guardado en %s", video.path().c_str()));
    };
    // Into `path`, or a new file in the recordings folder.
    auto startVideo = [&](GameRecording::Format format, std::string path = {}) {
        if (!app->romLoaded) return;
        const bool gif = format == GameRecording::Format::Gif;
        int width, height;
        if (recordsConsole()) {
            // As wide as the program's recordings are; a video three times that.
            const VideoFrame& f = app->console->frame();
            const int scale = gif ? 1 : 3;
            width = kRecordWidth * scale;
            height = (f.interlaced ? f.height / 2 : f.height) * scale;
        } else {
            // The screen, made smaller by a whole number: a GIF to about 480x360, a video
            // to 720 lines at most.
            int w, h;
            SDL_GL_GetDrawableSize(window, &w, &h);
            width = w;
            height = std::max(1, h - ui->menuHeight());
            const int k = gif ? std::max((width + 479) / 480, (height + 359) / 360) : (height + 719) / 720;
            width = std::max(2, width / k / 2 * 2);
            height = std::max(2, height / k / 2 * 2);
        }
        if (path.empty()) {
            char stamp[32] = "";
            const std::time_t now = std::time(nullptr);
            std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", std::localtime(&now));
            path = paths.videos + withoutExtension(fileName(app->romPath)) + "_" + stamp + (gif ? ".gif" : ".avi");
        }
        std::string error;
        if (!video.start(path, format, width, height, app->refreshRate(), audio->freq(), &error)) {
            ui->showError(error);
            return;
        }
        ui->notify(gif ? tr("Grabando un GIF (Ctrl+G para pararlo)") : tr("Grabando vídeo (Ctrl+R para pararlo)"));
    };
    // The same kind again stops it; the other kind takes its place.
    auto toggleVideo = [&](GameRecording::Format format) {
        const bool same = video.active() && video.format() == format;
        stopVideo();
        if (!same) startVideo(format);
    };
    auto load = [&](const std::string& path, int test = -1) {
        extras.beforeLoad(path);
        stopVideo();
        useGameOptions(path);  // before loading: the region may be the game's own
        if (app->load(path, capturing ? nullptr : window, test)) {
            extras.loaded(path);
            if (settings.achievements && !capturing) achievements.loadGame(*app->console, app->achievementsHash);
            settings.addRecent(path);
            settings.addHistory(path);  // the library's Histórico tab
            if (!game.own.empty()) ui->notify(trf("Opciones propias de %s", fileName(path).c_str()));
            extras.loadNotices();
            rewind.clear();
            setRewindLimits();
            runAheadFailed = false;
        } else {
            useGameOptions(app->romLoaded ? app->romPath : std::string());
            extras.loadFailed(path);
        }
        save();
    };
    auto setFullscreen = [&] { SDL_SetWindowFullscreen(window, settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); };
    if (settings.fullscreen) setFullscreen();
    const int testPattern = cmd.app.testPattern();
    if (!cmd.rom.empty() || testPattern >= 0) {
        if (testPattern >= 0) load(cmd.rom.empty() ? std::string("test-pattern") : cmd.rom, testPattern);
        else load(cmd.rom);
        applyCommandLine();  // over the game's profile too
        if (cmd.slot >= 0 && app->romLoaded && !hardcore()) ui->notify(app->loadState(cmd.slot));
        extras.commandLineLoaded();
        if (!cmd.record.empty()) {
            const bool gif = cmd.record.size() > 4 && cmd.record.compare(cmd.record.size() - 4, 4, ".gif") == 0;
            startVideo(gif ? GameRecording::Format::Gif : GameRecording::Format::Avi, cmd.record);
        }
    }
    bool openStateMenu = cmd.stateMenu;
    if (!cmd.library.empty()) settings.libraryFolders = cmd.library;
    if (cmd.libraryList) settings.libraryList = true;
    if (cmd.libraryBoxes >= 0) {
        settings.libraryBoxes = true;
        ui->poseBoxes(cmd.libraryBoxes);
    }
    // In console mode the library is the start screen even without folders: it asks for one.
    if (!cmd.library.empty() ||
        (cmd.rom.empty() && testPattern < 0 && !capturing && settings.libraryAtStart && (!settings.libraryFolders.empty() || consoleStart)))
        ui->toggleLibrary();
    if (cmd.search) ui->focusLibrarySearch();
    extras.openWindows(cmd.app, 0);
    if (cmd.achievements) ui->openAchievements();
    extras.openWindows(cmd.app, 1);
    int loops = 0;
    int media = cmd.media;
    bool sizedForMenu = false;

    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    Uint64 next = SDL_GetPerformanceCounter();
    bool running = true;
    bool paused = false;
    bool slowMotion = false;
    bool orbiting = false;  // dragging the pseudo-3D camera with the mouse
    bool padMenuWasOpen = false;  // the states menu, the library or the file browser, last frame
    int framesRun = 0;
    // The FPS counter's measures: the frames run in this turn of the loop and the time
    // spent running them, and when the last turn ended.
    FpsCounter fpsCounter;
    int turnFrames = 0;
    Uint64 turnEmu = 0;
    double turnTarget = 0;
    Uint64 lastTurnEnd = next;
    auto timed = [&](auto&& run) {
        const Uint64 start = SDL_GetPerformanceCounter();
        run();
        turnEmu += SDL_GetPerformanceCounter() - start;
    };
    RenderOptions& cam = settings.render;
    auto centreCamera = [&] {
        const RenderOptions defaults;
        cam.tilt = defaults.tilt;
        cam.yaw = defaults.yaw;
        cam.zoom = defaults.zoom;
        save();
    };
    auto saveState = [&](int slot) {
        if (!app->romLoaded) return;
        ui->notify(app->saveState(slot));
        std::error_code ec;
        if (achievements.gameLoaded()) writeFile(app->progressPath(slot), achievements.saveProgress());
        else std::filesystem::remove(std::filesystem::u8path(app->progressPath(slot)), ec);
    };
    auto loadState = [&](int slot) {
        if (!app->romLoaded) return;
        if (hardcore()) {
            ui->notify(tr("En modo hardcore no se pueden cargar estados"));
            return;
        }
        bool loaded = false;
        ui->notify(app->loadState(slot, &loaded));
        extras.stateLoaded(loaded);
        std::vector<uint8_t> progress;
        if (loaded && !readFile(app->progressPath(slot), progress)) progress.clear();
        if (loaded) achievements.loadProgress(progress);
    };
    // The console's reset button; the achievements start over with it.
    auto resetConsole = [&] {
        if (app->console) app->console->reset();
        achievements.reset();
    };
    auto toggleSlowMotion = [&] {
        slowMotion = !slowMotion;
        ui->notify(slowMotion ? trf("Cámara lenta (%d%%)", settings.slowMotion) : std::string(tr("Velocidad normal")));
    };
    auto setProfile = [&](bool on) {
        if (!app->romLoaded || on == game.has("profile_3d")) return;
        if (on) {
            game.own.insert("profile_3d");
            ui->notify(trf("El 3D se guarda ahora solo para %s", fileName(app->romPath).c_str()));
        } else {
            game.own.erase("profile_3d");
            copy3D(general.render, settings.render);
            ui->notify(tr("El juego vuelve a usar el 3D general"));
        }
        save();
    };
    auto setWideProfile = [&](bool on) {
        if (!app->romLoaded || on == game.has("widescreen")) return;
        if (on) {
            game.own.insert("widescreen");
            ui->notify(trf("La pantalla ancha se guarda ahora solo para %s", fileName(app->romPath).c_str()));
        } else {
            game.own.erase("widescreen");
            findGameOption("widescreen")->copy(general, settings);
            ui->notify(tr("El juego vuelve a usar la pantalla ancha general"));
        }
        save();
    };
    // The Opciones de este juego and Trucos windows can be for a game that is not loaded
    // (opened from the library): then they work on these copies, saved to its files.
    std::string optionsGame, cheatsGame;
    std::string windowsRom;  // the game loaded when they were opened
    GameOverrides otherOptions;
    std::vector<Cheat> otherCheats;
    auto openGameOptions = [&](const std::string& path) {
        optionsGame = path;
        if (path != app->romPath || !app->romLoaded) otherOptions = app->readOverrides(path, general, input->config());
    };
    auto openCheats = [&](const std::string& path) {
        cheatsGame = path;
        extras.cheatsFor(path);
        if (path != app->romPath || !app->romLoaded) otherCheats = app->readCheats(path);
    };
    extras.openWindows(cmd.app, 2);
    if (cmd.cheats && app->romLoaded) {
        openCheats(app->romPath);
        ui->openCheats();
    }
    if (cmd.gameOptions && app->romLoaded) {
        openGameOptions(app->romPath);
        ui->openGameOptions(cmd.gameOptions == 2);
    }
    auto stopNet = [&](std::string why, bool error) {  // a copy: it may be the session's, gone below
        if (!net) return;
        net->end();
        net.reset();
        netSocket.reset();
        if (app->romLoaded) app->applyCheats();
        extras.netStopped();
        rewind.clear();
        if (error) ui->showError(why);
        else if (!why.empty()) ui->notify(why);
    };
    auto startNet = [&](bool host) {
        stopNet("", false);
        if (!app->romLoaded) {
            ui->showError(tr("Abre primero el juego: los dos tenéis que tener abierto el mismo."));
            return;
        }
        auto socket = std::make_unique<UdpTransport>();
        if (!socket->open(host ? settings.netPort : 0) ||
            (!host && !socket->setPeer(settings.netAddress, settings.netPort))) {
            ui->showError(socket->error());
            return;
        }
        NetplaySession::Config config;
        config.inputDelay = settings.netInputDelay;
        net = std::make_unique<NetplaySession>(*socket, host ? NetplaySession::Role::Host : NetplaySession::Role::Guest,
                                               config);
        netSocket = std::move(socket);
        netPhase = net->phase();
        // ROM cheats change the cartridge, which the savestate the guest gets does not carry.
        app->console->setCheats({});
        // The guest plays on pad 2: it has to be plugged in (the guest gets the host's types).
        extras.netStarted();
        paused = slowMotion = false;
        ui->notify(host ? trf("Esperando al otro jugador en el puerto %d", settings.netPort)
                        : trf("Conectando con %s", settings.netAddress.c_str()));
    };
    // Parar emulación: back to the start screen (or the library) without closing the program.
    auto stop = [&] {
        if (!app->romLoaded) return;
        stopNet(tr("Has salido de la partida en red"), false);
        extras.stopping();
        stopVideo();
        save();  // the game's own options, while they are still its
        app->unload(capturing ? nullptr : window);
        achievements.unloadGame();
        useGameOptions(std::string());
        extras.stopped();
        rewind.clear();
        paused = slowMotion = false;
        save();
        ui->notify(tr("Emulación parada"));
        if (!settings.libraryFolders.empty() && !ui->libraryOpen()) ui->toggleLibrary();
    };
    auto selectSlot = [&](int delta) {
        settings.stateSlot = (settings.stateSlot + delta + kSlots) % kSlots;
        const SlotInfo& s = app->slots[static_cast<size_t>(settings.stateSlot)];
        ui->notify(s.used ? trf("Ranura %d (%s)", settings.stateSlot, s.when.c_str()) : trf("Ranura %d (vacía)", settings.stateSlot));
        save();
    };
    if (app->romLoaded && (cmd.netHost || !cmd.netJoin.empty())) {
        if (!cmd.netJoin.empty()) settings.netAddress = cmd.netJoin;
        startNet(cmd.netHost);
        ui->openNetplay();  // shows how the connection goes
    }
    while (running) {
        const Uint64 turnStart = SDL_GetPerformanceCounter();
        turnFrames = 0;
        turnEmu = 0;
        turnTarget = 0;
        // The picture's options (only the picture changes with them, so they can follow the
        // menus at any frame), and the overclock, which does change the game: in a netplay
        // game the speed is the host's, which came in the state both started from, and
        // hardcore mode keeps the real one.
        extras.everyTurn(!netPlaying(), hardcore() ? 100 : settings.overclock);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            input->handleEvent(e);  // first, so it sees every trigger pull and release
            if (ui->handleEvent(e)) continue;
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_DROPFILE:
                    stopNet(tr("Has salido de la partida en red"), false);
                    load(e.drop.file);
                    SDL_free(e.drop.file);
                    break;
                default: break;
            }
            // Full screen works over the library and the windows too: no game key or text box wants these.
            if (e.type == SDL_KEYDOWN && !e.key.repeat &&
                (e.key.keysym.scancode == SDL_SCANCODE_F11 ||
                 (e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)))) {
                settings.fullscreen = !settings.fullscreen;
                setFullscreen();
                save();
                continue;
            }
            if (ui->keyboardBusy() && e.type == SDL_KEYDOWN) continue;

            switch (input->hotkeyFor(e)) {
                case Hotkey::Toggle3D:
                    settings.render.pseudo3d = !settings.render.pseudo3d;
                    ui->notify(settings.render.pseudo3d ? tr("Efecto 3D activado") : tr("Efecto 3D desactivado"));
                    save();
                    break;
                case Hotkey::SaveState: saveState(settings.stateSlot); break;
                case Hotkey::LoadState:
                    if (!netPlaying()) loadState(settings.stateSlot);
                    break;
                case Hotkey::PrevSlot: selectSlot(-1); break;
                case Hotkey::NextSlot: selectSlot(1); break;
                case Hotkey::StateMenu: ui->toggleStateMenu(); break;
                case Hotkey::Library: ui->toggleLibrary(); break;
                case Hotkey::Stop: stop(); break;
                case Hotkey::SlowMotion:
                    if (app->romLoaded && !netPlaying() && !hardcore()) toggleSlowMotion();
                    break;
                case Hotkey::FpsCounter:
                    settings.fpsCounter = !settings.fpsCounter;
                    ui->notify(settings.fpsCounter ? tr("Contador de FPS activado") : tr("Contador de FPS desactivado"));
                    save();
                    break;
                default: break;  // rewind and fast forward work while held, below
            }
            // The pseudo-3D camera: drag to turn it, wheel to zoom, double click to centre it.
            if (cam.pseudo3d && !extras.mouseAims()) {  // with a light gun the mouse aims it
                // Without tilting the camera only the zoom changes.
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && !ui->mouseBusy() &&
                    cam.tiltCamera) {
                    if (e.button.clicks >= 2) centreCamera();
                    else orbiting = true;
                } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && orbiting) {
                    orbiting = false;
                    save();
                } else if (e.type == SDL_MOUSEMOTION && orbiting) {
                    cam.yaw = std::clamp(cam.yaw + 0.4f * static_cast<float>(e.motion.xrel), -kMaxYaw, kMaxYaw);
                    cam.tilt = std::clamp(cam.tilt + 0.4f * static_cast<float>(e.motion.yrel), kMinTilt, kMaxTilt);
                } else if (e.type == SDL_MOUSEWHEEL && !ui->mouseBusy()) {
                    cam.zoom = std::clamp(cam.zoom * std::pow(1.1f, static_cast<float>(e.wheel.y)), kMinZoom, kMaxZoom);
                    save();
                } else if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) {
                    centreCamera();
                }
            }
            if (e.type != SDL_KEYDOWN || e.key.repeat) continue;
            const bool ctrl = (e.key.keysym.mod & KMOD_CTRL) != 0;
            switch (e.key.keysym.scancode) {
                case SDL_SCANCODE_ESCAPE:
                    settings.showMenu = !settings.showMenu;
                    save();
                    break;
                case SDL_SCANCODE_O:
                    if (ctrl) ui->openRom();
                    break;
                case SDL_SCANCODE_L:
                    if (ctrl) ui->toggleLibrary();
                    break;
                case SDL_SCANCODE_R:
                    if (ctrl) toggleVideo(GameRecording::Format::Avi);
                    break;
                case SDL_SCANCODE_G:
                    if (ctrl) toggleVideo(GameRecording::Format::Gif);
                    break;
                case SDL_SCANCODE_F1:
                    if (!netPlaying() && app->romLoaded) resetConsole();
                    break;
                case SDL_SCANCODE_F2:
                    settings.view = renderers[(viewIndex() + 1) % renderers.size()]->name();
                    ui->notify(trf("Vista: %s", settings.view.c_str()));
                    save();
                    break;
                case SDL_SCANCODE_P:
                    if (!netPlaying()) paused = !paused;
                    break;
                case SDL_SCANCODE_M:
                    settings.mute = !settings.mute;
                    ui->notify(settings.mute ? tr("Sonido silenciado") : tr("Sonido activado"));
                    save();
                    break;
                default: break;
            }
        }

        // For screenshots, open it once a few frames have run.
        if (openStateMenu && (framesRun >= cmd.frames - 1 || !capturing)) {
            ui->toggleStateMenu(cmd.stateTab);
            openStateMenu = false;
        }
        // Another game loaded: the windows that were on the old one now edit its files.
        if (app->romPath != windowsRom) {
            windowsRom = app->romPath;
            if (!optionsGame.empty()) openGameOptions(optionsGame);
            if (!cheatsGame.empty()) openCheats(cheatsGame);
        }
        uiState.romLoaded = app->romLoaded;
        uiState.romPath = app->romPath;
        uiState.paused = paused;
        uiState.slowMotion = slowMotion;
        uiState.fps = &fpsCounter;
        uiState.hasProfile = game.has("profile_3d");
        uiState.hasWideProfile = game.has("widescreen");
        uiState.gameName = app->romLoaded ? fileName(app->romPath) : std::string();
        uiState.slots = app->slots;
        const bool cheatsLoaded = app->romLoaded && cheatsGame == app->romPath;
        uiState.cheats = cheatsGame.empty() ? nullptr : cheatsLoaded ? &app->cheats : &otherCheats;
        uiState.cheatsGame = cheatsGame.empty() ? std::string() : fileName(cheatsGame);
        uiState.cheatsPath = cheatsGame;
        const bool optionsLoaded = app->romLoaded && optionsGame == app->romPath;
        uiState.gameOptions.game = optionsGame.empty() ? std::string() : fileName(optionsGame);
        uiState.gameOptions.values = optionsLoaded ? &settings : &otherOptions.values;
        uiState.gameOptions.own = optionsLoaded ? &game.own : &otherOptions.own;
        uiState.gameOptions.general = &general;
        uiState.gameOptions.controls = optionsLoaded ? &game.controls : &otherOptions.controls;
        uiState.gameOptions.loaded = optionsLoaded;
        uiState.hardcore = hardcore();
        uiState.recording = !video.active() ? 0 : video.format() == GameRecording::Format::Gif ? 1 : 2;
        uiState.recordingSeconds = static_cast<int>(video.seconds());
        uiState.netSession = net != nullptr;
        uiState.netPlaying = netPlaying();
        uiState.netProgress = -1;
        if (net) {
            const bool host = net->role() == NetplaySession::Role::Host;
            switch (net->phase()) {
                case NetplaySession::Phase::Waiting:
                    uiState.netStatus = trf("Esperando al otro jugador en el puerto %d. Mientras, puedes seguir jugando: "
                                            "la partida empezará donde estés.", settings.netPort);
                    break;
                case NetplaySession::Phase::Connecting:
                    uiState.netStatus = trf("Conectando con %s...", settings.netAddress.c_str());
                    break;
                case NetplaySession::Phase::Syncing:
                    uiState.netStatus = host ? tr("Enviando la partida al otro jugador...") : tr("Recibiendo la partida del anfitrión...");
                    uiState.netProgress = net->syncProgress();
                    break;
                default:
                    uiState.netStatus = trf("Jugando en red como jugador %d.\nPing: %d ms. Retardo de los mandos: %d fotogramas. "
                                            "Vuelta atrás: %d fotogramas.", host ? 1 : 2, static_cast<int>(net->pingMs()),
                                            static_cast<int>(net->config().inputDelay), static_cast<int>(net->lastRollback()));
                    if (net->desyncFrame() >= 0)
                        uiState.netStatus += "\n" + trf("Las dos consolas se han descompasado (fotograma %d): lo que veis "
                                                        "ya no es lo mismo. Salid y volved a empezar.",
                                                        static_cast<int>(net->desyncFrame()));
                    break;
            }
        }
        extras.fillUiState(uiState, paused, net != nullptr);
        const UiActions act = ui->frame(uiState);
        if (!act.load.empty()) {
            stopNet(tr("Has salido de la partida en red"), false);
            load(act.load);
        }
        if (act.netHost) startNet(true);
        if (act.netJoin) startNet(false);
        if (act.netLeave) stopNet(tr("Has salido de la partida en red"), false);
        const bool netBusy = netPlaying();
        if (act.reset && !netBusy && app->romLoaded) resetConsole();
        if (act.stop) stop();
        if (act.record == 1) toggleVideo(GameRecording::Format::Gif);
        if (act.record == 2) toggleVideo(GameRecording::Format::Avi);
        if (act.openRecordings) {
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::u8path(paths.videos), ec);
            openInSystem(paths.videos);
        }
        if (act.togglePause && !netBusy) paused = !paused;
        if (act.quit) running = false;
        if (act.fullscreenChanged) setFullscreen();
        if (act.windowScale) applyWindowScale(window, act.windowScale, ui->menuHeight(), settings);
        if (act.saveState >= 0) saveState(act.saveState);
        if (act.loadState >= 0 && !netBusy) loadState(act.loadState);
        // A menu that owned the pad has closed (A loaded a state, Y saved one, B closed it):
        // that button is still down, and the game must not take it as a press of its own.
        const bool padMenuOpen = ui->stateMenuOpen() || ui->libraryOpen() || ui->browserOpen() || ui->padInWindows();
        if (padMenuWasOpen && !padMenuOpen) input->ignoreHeld();
        padMenuWasOpen = padMenuOpen;
        if (act.consoleChanged && !netBusy) {
            // A new region needs a fresh start, like switching a real console's region.
            applyConsole();
            if (app->romLoaded) resetConsole();
            rewind.clear();
            setRewindLimits();
        }
        if (act.toggleSlowMotion && !hardcore()) toggleSlowMotion();
        if (act.achievementsChanged) applyAchievements();
        // Hardcore switched on or off: the cheats, rewinding and slow motion follow.
        if (hardcore() != app->noCheats) {
            app->noCheats = hardcore();
            if (!net) app->applyCheats();
            if (app->noCheats) {
                slowMotion = false;
                rewind.clear();
            }
        }
        if (act.rewindChanged) setRewindLimits();
        if (act.profile >= 0) setProfile(act.profile == 1);
        if (act.wideProfile >= 0) setWideProfile(act.wideProfile == 1);
        if (!act.cheatsFor.empty()) openCheats(act.cheatsFor);
        if (act.cheatsChanged && cheatsLoaded) {
            if (!net) app->applyCheats();
            app->saveCheats();
        } else if (act.cheatsChanged && !cheatsGame.empty()) {
            app->writeCheats(cheatsGame, otherCheats);
        }
        if (!act.optionsFor.empty()) openGameOptions(act.optionsFor);
        if (act.gameOptionsChanged && optionsLoaded) {
            applyControls();
            save();
            if (act.rewindChanged) setRewindLimits();
        } else if (act.gameOptionsChanged && !optionsGame.empty()) {
            app->writeOverrides(optionsGame, otherOptions);
        }
        if (act.settingsChanged) save();
        extras.actions(act, paused, padMenuOpen);
        // The first frame tells us how tall the menu bar is; grow the window to fit it.
        if (!sizedForMenu && !settings.fullscreen && cmd.windowW <= 0) {
            applyWindowScale(window, settings.windowScale, ui->menuHeight(), settings);
            sizedForMenu = true;
        }

        const double frameSeconds = 1.0 / app->refreshRate();
        float stickX, stickY;
        input->rightStick(&stickX, &stickY);
        // In the library the right stick zooms its 3D view; in the game it turns the camera.
        ui->setRightStick(ui->libraryOpen() ? stickX : 0, ui->libraryOpen() ? stickY : 0);
        // And the left one turns the box or cartridge taken out there.
        float leftX = 0, leftY = 0;
        if (ui->libraryOpen()) input->leftStick(&leftX, &leftY);
        ui->setLeftStick(leftX, leftY);
        if (cam.pseudo3d && cam.tiltCamera && !ui->libraryOpen()) {
            // The right stick turns the camera at up to 90 degrees a second.
            const float sx = stickX, sy = stickY;
            const float step = 90.0f * static_cast<float>(frameSeconds);
            cam.yaw = std::clamp(cam.yaw + sx * step, -kMaxYaw, kMaxYaw);
            cam.tilt = std::clamp(cam.tilt + sy * step, kMinTilt, kMaxTilt);
        }

        uiState.speed.clear();
        bool ranFrame = false;
        if (net) {
            net->poll(*app->console, SDL_GetTicks());
            const NetplaySession::Phase phase = net->phase();
            const bool host = net->role() == NetplaySession::Role::Host;
            if (phase != netPhase) {
                if (phase == NetplaySession::Phase::Syncing) {
                    rewind.clear();
                    if (!host) app->hostsBattery = true;  // the host's state is coming
                }
                if (phase == NetplaySession::Phase::Playing) {
                    ui->notify(trf("¡Conectados! Eres el jugador %d", host ? 1 : 2));
                    framesRun = 0;  // --cuadros counts the frames played together
                }
                netPhase = phase;
            }
            if (phase == NetplaySession::Phase::Ended) stopNet(net->endReason(), true);
        }
        if (app->romLoaded && netPlaying()) {
            // A netplay game never waits for menus: the other player is still playing.
            if (net->phase() == NetplaySession::Phase::Playing) {
                const bool keyboard = !ui->keyboardBusy();
                if (!net->shouldWait()) {
                    app->console->setSampleRate(audio->rate());
                    // The menus that own the pad do not stop a netplay game, but their presses are not the game's.
                    bool advanced = false;
                    timed([&] { advanced = net->advance(*app->console, padMenuOpen ? PadState{} : input->poll(0, keyboard)); });
                    turnTarget = app->console->refreshRate();
                    if (advanced) {
                        achievements.doFrame();
                        ranFrame = true;
                        ++turnFrames;
                        ++framesRun;
                        audio->push(app->console->audio().samples(), settings.volume, settings.mute || capturing);
                        video.addAudio(app->console->audio().samples().data(), app->console->audio().samples().size());
                        app->console->audio().clearSamples();
                    }
                }
                uiState.speed = net->desyncFrame() >= 0 ? std::string(tr("Red: ¡descompasados!"))
                                                        : trf("Red %d ms", static_cast<int>(net->pingMs()));
            } else {
                uiState.speed = net->phase() == NetplaySession::Phase::Syncing ? tr("Red: preparando la partida")
                                                                               : tr("Red: conectando");
            }
            pacer.reset();
        }
        // The program's music player (the instrument editor's) is playing: the game waits.
        else if (extras.playMusic(frameSeconds, *audio, pacer, turnTarget)) {
        }
        // The game waits while the states menu, the library or the file browser is open.
        else if (app->romLoaded && !paused && !padMenuOpen) {
            const bool keyboard = !ui->keyboardBusy();
            const bool fast = input->held(Hotkey::FastForward, keyboard);
            const bool canRewind = settings.rewindSeconds > 0 && !hardcore();
            if (canRewind && input->held(Hotkey::Rewind, keyboard)) {
                // Back one frame per tick: load the newest snapshot and run that frame
                // again (silently, without the pads) to have its picture on screen.
                std::vector<uint8_t> snapshot;
                std::string error;
                if (rewind.pop(snapshot) && app->console->loadState(snapshot, &error)) {
                    extras.rewound();
                    app->console->setPads(PadState{}, PadState{});
                    app->console->runFrame();
                    app->console->audio().clearSamples();
                    uiState.speed = tr("<< Rebobinando");
                } else {
                    uiState.speed = tr("<< No se puede rebobinar más");
                }
                pacer.reset();
            } else {
                // Fast forward and slow motion run more or fewer frames per tick; the
                // sound follows, higher or lower, because each frame's samples are made
                // for 1/speed of a tick.
                const double musicSpeed = hardcore() ? 1.0 : extras.musicSpeed();  // the Partitura's x0.5 and x2
                const double speed = fast ? settings.fastForward : slowMotion ? settings.slowMotion / 100.0 : musicSpeed;
                if (fast) uiState.speed = ">> x" + std::to_string(settings.fastForward);
                else if (slowMotion) uiState.speed = trf("Cámara lenta %d%%", settings.slowMotion);
                else if (musicSpeed != 1.0) uiState.speed = musicSpeed < 1 ? "x0.5" : "x2";
                // Run-ahead: the last frame of the tick is followed by a few more with the
                // same pads, whose picture is shown, and the console goes back. Not in fast
                // forward, where a frame or two less of lag is not felt.
                const int ahead = fast || runAheadFailed ? 0 : settings.runAhead;
                turnTarget = app->console->refreshRate() * speed;
                for (int n = pacer.framesForTick(speed); n > 0; --n) {
                    if (canRewind) {
                        std::vector<uint8_t> snapshot;
                        if (app->console->saveState(snapshot, true)) rewind.push(std::move(snapshot));
                    }
                    app->console->setPads(input->poll(0, keyboard), input->poll(1, keyboard));
                    extras.aim(keyboard);
                    app->console->setSampleRate(audio->rate() / speed);
                    // The picture shown is run-ahead's, so these need not be drawn.
                    app->console->setDrawing(ahead == 0);
                    timed([&] { app->console->runFrame(); });
                    achievements.doFrame();
                    ranFrame = true;
                    ++framesRun;
                    ++turnFrames;
                    audio->push(app->console->audio().samples(), settings.volume, settings.mute || capturing);
                    video.addAudio(app->console->audio().samples().data(), app->console->audio().samples().size());
                    app->console->audio().clearSamples();
                    bool aheadFailed = false;
                    if (ahead > 0 && n == 1) timed([&] { aheadFailed = !app->console->runAhead(ahead); });
                    if (aheadFailed) {
                        runAheadFailed = true;
                        ui->notify(tr("Run-ahead desactivado: no se pudo volver al fotograma actual."));
                    }
                }
                app->console->setDrawing(true);
            }
        }

        // RetroAchievements: its answers, and what it has to say.
        if (!ranFrame) achievements.idle();
        achievements.poll();
        for (const Achievements::Notice& n : achievements.takeNotices()) {
            if (n.kind == Achievements::Notice::Kind::Reset && app->romLoaded) {
                resetConsole();
                rewind.clear();
            }
            ui->showAchievement(n);
        }
        if (achievements.loggedIn() && achievements.token() != settings.raToken) {
            settings.raToken = achievements.token();
            save();
        }

        int w, h;
        SDL_GL_GetDrawableSize(window, &w, &h);
        gl::Viewport(0, 0, w, h);
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT);
        // The game fills the window below the menu bar; OpenGL counts rows from the bottom.
        const Viewport gameArea{0, 0, w, std::max(1, h - ui->menuHeight())};
        Renderer& view = settings.render.pseudo3d ? *pseudo3d : *renderers[viewIndex()];
        view.setOptions(settings.render);
        if (app->romLoaded) view.draw(app->console->frame(), gameArea);  // stopped: the start screen
        // A recording takes the game's picture before the menus are drawn over it.
        if (video.active() && app->romLoaded) {
            if (recordsConsole()) {
                const VideoFrame& f = app->console->frame();
                video.addFrame(f.rgba.data(), f.width, f.height, VideoFrame::kMaxWidth);
            } else {
                screen.resize(static_cast<size_t>(gameArea.width) * gameArea.height);
                gl::ReadPixels(0, 0, gameArea.width, gameArea.height, gl::BGRA, gl::UNSIGNED_INT_8_8_8_8_REV,
                               screen.data());
                video.addFrame(screen.data() + static_cast<size_t>(gameArea.height - 1) * gameArea.width,
                               gameArea.width, gameArea.height,
                               -static_cast<ptrdiff_t>(gameArea.width));  // OpenGL's rows go bottom up
            }
            if (video.failed()) stopVideo();
        }
        ui->render();
        ++loops;
        if (media && ui->libraryOpen() && ui->library().pending() == 0 && loops > 3) {
            ui->startMedia(0, media);
            media = 0;
            loops = 0;
        }
        // A screenshot waits a few frames for the window and the popups to settle, and a
        // library one for its covers.
        const bool libraryReady =
            loops > 3 && (!ui->libraryOpen() || (!media && ui->library().pending() == 0 && loops > 20)) &&
            !ui->mediaBusy() && !ui->cheatsBusy() && !ui->libraryInfoBusy();
        if (capturing && libraryReady &&
            (framesRun >= cmd.frames || !app->romLoaded || ui->libraryOpen() ||
             (ui->stateMenuOpen() && framesRun >= cmd.frames - 1))) {
            const bool ok = captureWindow(window, cmd.capture);
            std::printf("%s %s\n", ok ? "captura guardada en" : "no se pudo guardar", cmd.capture.c_str());
            running = false;
        }
        SDL_GL_SwapWindow(window);
        {
            const Uint64 end = SDL_GetPerformanceCounter();
            fpsCounter.tick(static_cast<double>(end - lastTurnEnd) / freq, turnFrames, static_cast<double>(turnEmu) / freq,
                            static_cast<double>(end - turnStart) / freq,
                            turnTarget > 0 ? turnTarget : app->refreshRate());
            lastTurnEnd = end;
        }
        if (capturing) continue;  // as fast as possible

        // Pace to the console's refresh rate; resync if we fell far behind.
        next += static_cast<Uint64>(frameSeconds * freq);
        Uint64 now = SDL_GetPerformanceCounter();
        if (now < next) {
            SDL_Delay(static_cast<Uint32>((next - now) * 1000 / freq));
        } else if (now - next > static_cast<Uint64>(freq / 4)) {
            next = now;
        }
    }

    if (net) net->end();
    net.reset();
    netSocket.reset();
    extras.stopping();
    stopVideo();
    app->saveBattery();
    save();
    for (auto& s : app->slots)
        if (s.texture) gl::DeleteTextures(1, &s.texture);
    audio.reset();
    ui.reset();
    renderers.clear();
    pseudo3d.reset();
    input.reset();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
