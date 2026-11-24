#include "frontend/app_main.h"

#include <algorithm>
#include <array>
#include <filesystem>

#include "../core/system.h"
#include "render/profile3d.h"

namespace plumsg {

const char* const kHelp = N_(
    "plumsg [juego] [opciones]\n\n"
    "  juego                  cartucho (.md .gen .bin .smd .32x .sms .gg .zip) o disco (.cue .chd .iso)\n"
    "  --fullscreen           abrir a pantalla completa\n"
    "  --windowed             abrir en ventana\n"
    "  --modo-consola         para jugar con mando (Steam Deck, ROG Ally...): pantalla completa,\n"
    "                         biblioteca y letra grande (--modo-escritorio para no usarlo)\n"
    "  --3d / --2d            con o sin la vista pseudo-3D\n"
    "  --filter f             none, scanlines o crt\n"
    "  --upscaling u          none, xbr o anime4k (experimental)\n"
    "  --fps                  mostrar el contador de FPS con su gráfica\n"
    "  --region r             auto, jp, us o eu\n"
    "  --load-state N         cargar el estado guardado N (0-9) del juego\n"
    "  --host                 alojar una partida en red con el juego (puerto del menú Red)\n"
    "  --join direccion       unirse a la partida en red de ese anfitrión (IP:puerto)\n"
    "  --library carpeta      abrir la biblioteca de esa carpeta (se puede repetir)\n"
    "  --bios                 abrir la ventana de las BIOS del Mega CD\n"
    "  --list                 con --library: verla como lista en vez de cuadrícula\n"
    "  --boxes                con --library: verla como cajas 3D\n"
    "  --cheats               abrir los trucos del juego, con sus listas de internet\n"
    "  --achievements         abrir la ventana de RetroAchievements\n"
    "  --game-options         abrir las opciones propias del juego\n"
    "  --game-options controls   ...en la pestaña de sus controles\n"
    "  --instruments          abrir el editor de instrumentos (del juego, si se da)\n"
    "  --track f.vgm          con --instruments: abrir esa música (.vgm o .vgz)\n"
    "  --seek s               con --track: tocarla desde ese segundo\n"
    "  --score                abrir la Partitura\n"
    "  --record f.avi         grabar la partida desde el principio, en vídeo (.avi) o GIF (.gif)\n"
    "  --screenshot f.png     guardar una captura tras --frames N fotogramas y salir\n\n"
    "La pantalla completa y la región dadas aquí valen solo para esta vez. Las opciones\n"
    "en español de las versiones anteriores (--pantalla-completa, --biblioteca...) siguen valiendo.");

bool AppCommandLine::parse(const std::string& a, int& i, int argc, char** argv) {
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--bios") bios = true;
    else if (a == "--instruments") editor = true;
    else if (a == "--track") track = next();
    else if (a == "--seek") seek = std::atof(next().c_str());
    else if (a == "--score") score = true;
    else return false;
    return true;
}

void newConsole(std::unique_ptr<GameConsole>& console) { console = std::make_unique<MegaDrive>(); }

bool openConsole(std::unique_ptr<GameConsole>& console, const std::string& path, int, std::string* error) {
    return console->loadRom(path, error);
}

void closeConsole(std::unique_ptr<GameConsole>& console) { console->setCheats({}); }

std::string consoleTitle(GameConsole& console) { return console.cartridge().header().title; }

std::vector<CheatPatch> decodeCheats(GameConsole& console, const std::vector<Cheat>& cheats) {
    std::vector<CheatPatch> patches;
    for (const Cheat& c : cheats) {
        std::vector<CheatPatch> p;
        if (c.enabled && decodeCheatCodes(c.code, &p, nullptr, console.isMasterSystem())) patches.insert(patches.end(), p.begin(), p.end());
    }
    return patches;
}

void saveBattery(GameConsole& console, const std::string& path) {
    if (MegaCd* cd = console.megaCd(); cd && !console.hasMsuMd()) {
        if (cd->backupRamChanged() && writeFile(path, std::vector<uint8_t>(cd->backupRam().begin(), cd->backupRam().end())))
            cd->clearBackupRamChanged();
        return;
    }
    if (console.cartridge().hasSaveData()) console.cartridge().writeSave(path);
}

void loadBattery(GameConsole& console, const std::string& path) {
    MegaCd* cd = console.megaCd();
    if (!cd || console.hasMsuMd()) {  // an MSU-MD game saves on its cartridge
        console.cartridge().loadSave(path);
        return;
    }
    std::vector<uint8_t> data;
    if (readFile(path, data) && data.size() == cd->backupRam().size())
        std::copy(data.begin(), data.end(), cd->backupRam().begin());
    else
        cd->formatBackupRam();
    cd->clearBackupRamChanged();
}

// Up to 0.0.31 the folders and some files next to plumsg.ini had Spanish names. Renames
// them once to the English ones; a folder whose new name already exists is left alone.
void AppMain::prepareFolders(const Paths&, const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::u8path(dir);
    static const std::pair<const char*, const char*> kFolders[] = {
        {"perfiles", "profiles"}, {"trucos", "cheats"}, {"caratulas", "covers"}, {"fichas", "game-info"},
        {"sonido", "sound"}, {"grabaciones", "recordings"}, {"fallos", "crash-reports"}, {"sound/pistas", "sound/tracks"}};
    for (const auto& [es, en] : kFolders) {
        const fs::path from = base / fs::u8path(es), to = base / fs::u8path(en);
        if (fs::is_directory(from, ec) && !fs::exists(to, ec)) fs::rename(from, to, ec);
    }
    // Files that only change their extension.
    static const std::array<std::array<const char*, 3>, 3> kFiles = {{{"states", ".logros", ".achievements"},
                                                                       {"sound", ".efectos", ".effects"},
                                                                       {"crash-reports", "ultimo-fallo.txt", "last-crash.txt"}}};
    for (const auto& [folder, es, en] : kFiles) {
        for (fs::directory_iterator it(base / folder, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().u8string();
            const std::string old = es;
            if (name.size() < old.size() || name.compare(name.size() - old.size(), old.size(), old) != 0) continue;
            const fs::path to = it->path().parent_path() / fs::u8path(name.substr(0, name.size() - old.size()) + en);
            std::error_code e;
            if (!fs::exists(to, e)) fs::rename(it->path(), to, e);
        }
        ec.clear();
    }
}

void AppMain::setup(MainContext& m) {
    m_ = &m;
    editor_ = std::make_unique<InstrumentEditor>(m.paths.sound);
    m.ui.setEditor(editor_.get());
    m.ui.setSoundfonts(m.paths.soundfont, m.paths.sound + "soundfonts/");
    m.ui.musicBatch().setFolder(m.paths.sound);
}

void AppMain::applyConsole() { m_->app.console->setMasterSystemFm(m_->settings.smsFm); }

bool AppMain::loadSoundfont() {
    const Settings& settings = m_->settings;
    const std::string path = settings.soundfont.empty() ? m_->paths.soundfont : settings.soundfont;
    if (path == rolandFont_) return rolandFontOk_;
    rolandFont_ = path;
    std::vector<uint8_t> data;
    rolandFontOk_ = readFile(path, data) && roland_->loadSoundfont(data);
    if (!rolandFontOk_ && !m_->capturing)
        m_->ui.showError(trf("No se pudo abrir el SoundFont %s.\nSe oye el sonido original.", path.c_str()));
    return rolandFontOk_;
}

// The game's instruments: those chosen by hand, and its sound effects.
void AppMain::applyInstruments(const std::string& path) {
    const std::string& sound = m_->paths.sound;
    App& app = m_->app;
    roland_->setAssignments(InstrumentEditor::assignmentsFor(sound, path));
    roland_->setSoundEffects(InstrumentEditor::effectsFor(sound, path), m_->settings.effectsOriginal);
    // Its songs: what was chosen for each alone, and how the game asks for them, for the
    // console to tell which one it plays.
    std::vector<SongRequest> ways;
    roland_->setSongs(InstrumentEditor::songsFor(sound, path, &ways));
    app.console->setSongRequests(app.romLoaded && path == app.romPath ? std::move(ways) : std::vector<SongRequest>{});
}

void AppMain::applySound() {
    const Settings& settings = m_->settings;
    MegaDrive& console = *m_->app.console;
    // A Mega CD disc's music is CD audio, already better than a SoundFont: there the
    // FM and PSG only play sound effects, which stay as they are. So for an MSU-MD
    // cartridge, which plays its music from CD tracks too.
    const bool on = settings.roland && !console.megaCd() && loadSoundfont();
    // With the chips' own sound, the notes are still followed for the Partitura and
    // to switch instruments off.
    const bool follow = !on && !console.megaCd() && (m_->ui.scoreOpen() || !gameSilenced_.empty());
    roland_->setFollowOnly(follow);
    roland_->setEffects(settings.reverb, settings.chorus);
    roland_->setEqualizer(settings.equalizer);
    roland_->setTimbreMatching(settings.compareSound && on);
    console.setRoland(on || follow ? roland_.get() : nullptr, follow ? 0 : settings.chipMix);
}

void AppMain::gameOptionsUsed(const std::string& path) {
    gameSilenced_.clear();
    roland_->setListening(0, {});
    applySound();
    applyInstruments(path);
}

void AppMain::stopRecording() {
    if (!recording_) return;
    recording_ = false;
    App& app = m_->app;
    app.console->setVgmWriter(nullptr);
    editor_->saveRecording(recorder_.finish(app.console->masterClocks(), withoutExtension(fileName(app.romPath))),
                           app.romPath);
}

void AppMain::beforeLoad(const std::string& path) {
    stopRecording();
    const ConsoleSystem system = detectSystem(path);
    if (system == ConsoleSystem::MegaCD || !msuMdCue(path).empty()) {
        // Discs boot with the BIOS of their region, from the files the user chose, and
        // MSU-MD cartridges with any of them.
        static const Region regions[3] = {Region::JapanNtsc, Region::UsaNtsc, Region::EuropePal};
        for (size_t i = 0; i < 3; ++i) {
            std::vector<uint8_t> bios;
            if (m_->settings.cdBios[i].empty() || !readFile(m_->settings.cdBios[i], bios) || bios.size() != 0x20000)
                bios.clear();
            m_->app.console->setCdBios(regions[i], std::move(bios));
        }
    }
}

void AppMain::loaded(const std::string& path) {
    applySound();            // now it is known whether it is a disc
    applyInstruments(path);  // the console's song requests, now that it has the game
}

void AppMain::loadNotices() {
    MegaDrive& console = *m_->app.console;
    Ui& ui = m_->ui;
    if (isLightGun(console.padType(1)) && !isLightGun(console.chosenPadType(1)))
        ui.notify(console.padType(1) == PadType::Menacer
                      ? tr("Este juego usa la pistola Menacer: ya está en el puerto 2 (apunta con el ratón)")
                      : tr("Este juego usa la pistola Justifier: ya está en el puerto 2 (apunta con el ratón)"));
    if (!console.loadNotice().empty()) ui.notify(console.loadNotice());
    else if (console.hasMsuMd()) ui.notify(tr("Música en CD (MSU-MD)"));
}

void AppMain::loadFailed(const std::string& path) {
    const std::string& lastError = m_->app.lastError;
    // A missing BIOS already says what to do; anything else is the file's fault.
    const bool bios = lastError.find("BIOS") != std::string::npos;
    if (!m_->capturing)
        m_->ui.showError(bios ? lastError
                              : trf("No se pudo abrir %s:\n%s\n\nplumsg abre cartuchos de Mega Drive, 32X, Master System y "
                                    "Game Gear y discos de Mega CD. Si el archivo es uno de ellos, puede estar "
                                    "incompleto o dañado: prueba a copiarlo otra vez.",
                                    fileName(path).c_str(), lastError.c_str()));
    if (bios) m_->ui.openBios();  // the game is fine: it waits for its BIOS
    else m_->settings.removeRecent(path);
}

void AppMain::openWindows(const AppCommandLine& cmd, int stage) {
    Ui& ui = m_->ui;
    if (stage == 0 && cmd.bios) ui.openBios();
    if (stage != 1) return;
    if (cmd.editor) {
        const Settings& settings = m_->settings;
        loadSoundfont();
        editor_->useSoundfont(*roland_, settings.reverb, settings.chorus, settings.equalizer);
        editor_->open(m_->app.romLoaded ? m_->app.romPath : std::string());
        if (!cmd.track.empty()) editor_->openTrack(cmd.track);
        if (cmd.seek >= 0 && editor_->trackLoaded()) {
            editor_->seekTrack(cmd.seek);
            editor_->togglePlay();
        }
    }
    if (cmd.score) {
        ui.openScore();
        applySound();
    }
    lastAim_ = SDL_GetPerformanceCounter();
}

void AppMain::cheatsFor(const std::string& path) {
    const ConsoleSystem system = detectSystem(path);
    cheatsSms_ = system == ConsoleSystem::MasterSystem || system == ConsoleSystem::GameGear;
}

void AppMain::netStarted() {
    MegaDrive& console = *m_->app.console;
    // A light gun's aim does not travel over the network either.
    if (console.padType(1) == PadType::None || isLightGun(console.padType(1)) || isMasterSystemController(console.padType(1)))
        console.setPadType(1, PadType::SixButton);
    if (isMasterSystemController(console.padType(0))) console.setPadType(0, PadType::SixButton);
    console.setAutoControllers(false);
}

void AppMain::netStopped() {
    MegaDrive& console = *m_->app.console;
    console.setPadType(0, m_->settings.padType[0]);
    console.setPadType(1, m_->settings.padType[1]);
    console.setAutoControllers(true);
}

void AppMain::everyTurn(bool canOverclock, int overclock) {
    MegaDrive& console = *m_->app.console;
    const Settings& settings = m_->settings;
    console.vdp().setUnlimitedSprites(settings.unlimitedSprites);
    if (MasterSystem* sms = console.masterSystem()) sms->vdp().setUnlimitedSprites(settings.unlimitedSprites);
    console.vdp().setWidescreen(settings.wideLeft, settings.wideRight);
    if (canOverclock) console.setOverclock(overclock);
}

bool AppMain::smsAimed(int port) const {
    const App& app = m_->app;
    return app.romLoaded && app.console->isMasterSystem() && isMasterSystemController(app.console->controller(port));
}

bool AppMain::mouseAims() const {
    const App& app = m_->app;
    if (!app.romLoaded) return false;
    if (app.console->isMasterSystem()) return smsAimed(0) || smsAimed(1);
    return isLightGun(app.console->padType(1));
}

void AppMain::fillUiState(UiState& uiState, bool paused, bool netSession) {
    App& app = m_->app;
    MegaDrive& console = *app.console;
    Ui& ui = m_->ui;
    uiState.cheatsSms = cheatsSms_;
    uiState.editor.loadedGame = app.romLoaded ? app.romPath : std::string();
    uiState.editor.recording = recording_;
    uiState.editor.recordedSeconds = recorder_.secondsAt(console.masterClocks());
    uiState.editor.soundfontReady = rolandFontOk_;
    // The Partitura: the editor's track while it has one open, else the game's notes.
    scoreMusic_ = ScoreMusic{};
    trackScore_ = editor_->isOpen() && editor_->trackLoaded();
    if (trackScore_) {
        scoreMusic_.notes = &editor_->trackScore();
        scoreMusic_.clockRate = editor_->trackClock();
        scoreMusic_.now = editor_->trackPosition();
        scoreMusic_.length = editor_->trackLength();
        scoreMusic_.playing = editor_->playing();
        scoreMusic_.track = true;
        scoreMusic_.sound = &editor_->synth();
        scoreMusic_.silenced = &editor_->silencedInstruments();
    } else if (app.romLoaded && console.rolandSound()) {
        scoreMusic_.notes = &roland_->score();
        scoreMusic_.now = static_cast<double>(roland_->scoreClock()) / RolandSound::kScoreClock;
        scoreMusic_.playing = !paused;
        scoreMusic_.canSpeed = !m_->hardcore() && !netSession;
        scoreMusic_.sound = roland_.get();
        scoreMusic_.silenced = &gameSilenced_;
    }
    uiState.score = scoreMusic_.notes ? &scoreMusic_ : nullptr;
    uiState.disc = app.romLoaded && console.megaCd();
    // The light guns: the picture as the last loop drew it, the mouse where it went
    // (not over the menus), and each player's stick or d-pad.
    uiState.crosshairs.clear();
    uiState.hideMouse = false;
    if (mouseAims() && !m_->netPlaying()) {
        if (app.romPath != gunRom_) {
            gunRom_ = app.romPath;
            gunAim_.forget();
        }
        SDL_Window* window = m_->window;
        int ww, wh, dw, dh;
        SDL_GetWindowSize(window, &ww, &wh);
        SDL_GL_GetDrawableSize(window, &dw, &dh);
        const float scale = dw > 0 ? static_cast<float>(ww) / static_cast<float>(dw) : 1.0f;
        const VideoFrame& f = console.frame();
        const Viewport pic = pictureArea(f, Viewport{0, 0, dw, std::max(1, dh - ui.menuHeight())}, m_->settings.render);
        const float colW = static_cast<float>(pic.width) / static_cast<float>(f.width);
        gunAim_.setPicture((static_cast<float>(pic.x) + colW * static_cast<float>(f.wideLeft)) * scale,
                           static_cast<float>(dh - pic.y - pic.height) * scale,
                           colW * static_cast<float>(f.consoleWidth()) * scale, static_cast<float>(pic.height) * scale);
        int mx, my;
        const Uint32 mouse = SDL_GetMouseState(&mx, &my);
        const bool mouseFree = !ui.mouseBusy() && (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS);
        if (mouseFree && (mx != lastMouseX_ || my != lastMouseY_)) gunAim_.mouseMoved(static_cast<float>(mx), static_cast<float>(my));
        lastMouseX_ = mx;
        lastMouseY_ = my;
        gunAim_.mouseButtons(mouseFree && (mouse & SDL_BUTTON_LMASK), mouseFree && (mouse & SDL_BUTTON_RMASK),
                             mouseFree && (mouse & SDL_BUTTON_MMASK));
        const bool keyboard = !ui.keyboardBusy();
        const bool sms = console.isMasterSystem();
        const Uint64 nowAim = SDL_GetPerformanceCounter();
        const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
        const float seconds = std::min(0.1f, static_cast<float>(static_cast<double>(nowAim - lastAim_) / freq));
        lastAim_ = nowAim;
        bool anyGun = false;
        for (int g = 0; g < 2; ++g) {
            // A Master System game: each port's own (the paddle draws no crosshair).
            if (sms ? !smsAimed(g) : g == 1 && console.padType(1) != PadType::Justifier) continue;
            float sx, sy;
            m_->input.leftStick(g, &sx, &sy);
            if (!paused) gunAim_.move(g, m_->input.poll(g, keyboard), sx, sy, seconds);
            if (sms && console.controller(g) == PadType::Paddle) continue;
            // The Justifier's pink gun waits until the second player takes it.
            if (!sms && !gunAim_.used(g)) continue;
            anyGun = true;
            UiState::Crosshair c;
            c.onPicture = gunAim_.crosshair(g, &c.x, &c.y);
            c.gun = g;
            uiState.crosshairs.push_back(c);
        }
        uiState.hideMouse = anyGun && mouseFree && gunAim_.over(static_cast<float>(mx), static_cast<float>(my));
    }
}

void AppMain::actions(const UiActions& act, bool& paused, bool padMenuOpen) {
    App& app = m_->app;
    const Settings& settings = m_->settings;
    musicSpeed_ = act.musicSpeed;
    if (trackScore_) {
        if (act.score.togglePlay) editor_->togglePlay();
        if (act.score.stop) editor_->stopTrack();
        if (act.score.seek >= 0) editor_->seekTrack(act.score.seek);
        if (act.score.toggleSound) editor_->toggleInstrument(act.score.toggleSound);
    } else if (act.score.toggleSound) {
        if (!gameSilenced_.erase(act.score.toggleSound)) gameSilenced_.insert(act.score.toggleSound);
        roland_->setListening(0, gameSilenced_);
        applySound();
    }
    // Sacar la música de todos los juegos waits while a game is being played.
    m_->ui.musicBatch().setPaused(app.romLoaded && !paused && !padMenuOpen);
    if (act.equalizerChanged) {
        roland_->setEqualizer(settings.equalizer);
        editor_->useEqualizer(settings.equalizer);
    }
    if (act.soundChanged) applySound();
    if (act.soundChanged && app.romLoaded) applyInstruments(app.romPath);
    if (act.soundChanged && editor_->isOpen() && loadSoundfont())
        editor_->useSoundfont(*roland_, settings.reverb, settings.chorus, settings.equalizer);
    if (act.openEditor) {
        loadSoundfont();
        editor_->useSoundfont(*roland_, settings.reverb, settings.chorus, settings.equalizer);
        editor_->open(act.editorFor);
    }
    if (act.startRecording && app.romLoaded && !recording_) {
        recording_ = true;
        app.console->setVgmWriter(&recorder_);
        paused = false;
    }
    if (act.stopRecording || (recording_ && !editor_->isOpen())) stopRecording();
    if (act.assignmentsChanged && app.romLoaded) applyInstruments(app.romPath);
}

// The instrument editor is playing: the game waits, and its track is heard.
bool AppMain::playMusic(double frameSeconds, Audio& audio, FramePacer& pacer, double&) {
    if (!editor_->playing()) return false;
    const Settings& settings = m_->settings;
    std::vector<int16_t> samples;
    // At another speed the track plays more or less of itself in the same time:
    // the synthesizer's notes keep their pitch, the chips' sound goes higher or lower.
    editor_->play(frameSeconds * musicSpeed_, audio.rate() / musicSpeed_, samples);
    audio.push(samples, settings.volume, settings.mute || m_->capturing);
    pacer.reset();
    return true;
}

void AppMain::aim(bool keyboard) {
    if (!mouseAims()) return;
    MegaDrive& console = *m_->app.console;
    const VideoFrame& f = console.frame();
    const int lines = f.interlaced ? f.height / 2 : f.height;
    for (int g = 0; g < 2; ++g) {
        const PadState pad = m_->input.poll(g, keyboard);
        const GunState gun = gunAim_.state(g, pad, f.consoleWidth(), lines);
        console.setGun(g, gun);
        if (smsAimed(g) && console.controller(g) == PadType::Paddle) {
            PaddleState paddle;
            paddle.position = gunAim_.knob(g);
            paddle.button = gun.trigger || pad.b;
            console.setPaddle(g, paddle);
        }
    }
}

}  // namespace plumsg
