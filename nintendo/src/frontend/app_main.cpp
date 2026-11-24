#include "frontend/app_main.h"

#include <filesystem>

#include "../core/system.h"
#include "../core/test_console.h"
#include "instrument_editor.h"

namespace plumnin {

const char* const kHelp = N_(
    "plumnin [juego] [opciones]\n\n"
    "  juego                  cartucho (.gb .gbc .nes .sfc .smc .zip)\n"
    "  --fullscreen           abrir a pantalla completa\n"
    "  --windowed             abrir en ventana\n"
    "  --console-mode         para jugar con mando (Steam Deck, ROG Ally...): pantalla completa,\n"
    "                         biblioteca y letra grande (--desktop-mode para no usarlo)\n"
    "  --3d / --2d            con o sin la vista pseudo-3D\n"
    "  --filter f             none, scanlines, crt o lcd\n"
    "  --upscaling u          none, xbr o anime4k (experimental)\n"
    "  --fps                  mostrar el contador de FPS con su gráfica\n"
    "  --region r             auto, jp, us o eu\n"
    "  --load-state N         cargar el estado guardado N (0-9) del juego\n"
    "  --host                 alojar una partida en red con el juego (puerto del menú Red)\n"
    "  --join direccion       unirse a la partida en red de ese anfitrión (IP:puerto)\n"
    "  --library carpeta      abrir la biblioteca de esa carpeta (se puede repetir)\n"
    "  --list                 con --library: verla como lista en vez de cuadrícula\n"
    "  --boxes                con --library: verla como cajas 3D\n"
    "  --cheats               abrir los trucos del juego, con sus listas de internet\n"
    "  --instruments          abrir el editor de instrumentos\n"
    "  --achievements         abrir la ventana de RetroAchievements\n"
    "  --game-options         abrir las opciones propias del juego\n"
    "  --game-options controls   ...en la pestaña de sus controles\n"
    "  --test-pattern [gb|gbc|nes|snes]   abrir la imagen de prueba en vez de un juego\n"
    "  --record f.avi         grabar la partida desde el principio, en vídeo (.avi) o GIF (.gif)\n"
    "  --screenshot f.png     guardar una captura tras --frames N fotogramas y salir\n\n"
    "La pantalla completa y la región dadas aquí valen solo para esta vez.");

bool AppCommandLine::parse(const std::string& a, int& i, int argc, char** argv) {
    if (a == "--instruments") instruments = true;
    else if (a == "--test-pattern") {
        test = static_cast<int>(ConsoleSystem::Nes);
        const std::string s = i + 1 < argc ? argv[i + 1] : "";
        if (s == "gb") test = static_cast<int>(ConsoleSystem::GameBoy), ++i;
        else if (s == "gbc") test = static_cast<int>(ConsoleSystem::GameBoyColor), ++i;
        else if (s == "nes") ++i;
        else if (s == "snes") test = static_cast<int>(ConsoleSystem::Snes), ++i;
    } else return false;
    return true;
}

bool openConsole(std::unique_ptr<GameConsole>& console, const std::string& path, int test, std::string* error) {
    std::unique_ptr<Console> next;
    if (test >= 0) {
        next = std::make_unique<TestConsole>(static_cast<ConsoleSystem>(test));
        next->loadRomData(std::vector<uint8_t>(1, 0));
    } else {
        next = openGame(path, error);
    }
    if (!next) return false;
    console = std::move(next);
    return true;
}

void closeConsole(std::unique_ptr<GameConsole>& console) { console.reset(); }

std::string consoleTitle(GameConsole& console) { return console.title(); }

std::vector<CheatPatch> decodeCheats(GameConsole& console, const std::vector<Cheat>& cheats) {
    std::vector<CheatPatch> patches;
    const CheatDecoder decoder = console.cheatDecoder();
    for (const Cheat& c : cheats) {
        std::vector<CheatPatch> p;
        if (c.enabled && decodeCheatCodes(c.code, decoder, &p, nullptr)) patches.insert(patches.end(), p.begin(), p.end());
    }
    return patches;
}

void saveBattery(GameConsole& console, const std::string& path) {
    if (!console.hasSaveData() || !console.saveDataChanged()) return;
    if (writeFile(path, console.saveData())) console.clearSaveDataChanged();
}

void loadBattery(GameConsole& console, const std::string& path) {
    std::vector<uint8_t> data;
    if (console.hasSaveData() && readFile(path, data)) console.loadSaveData(data);
    console.clearSaveDataChanged();
}

void AppMain::settingsLoaded(const Paths& paths) {
    // Made empty at the first run, so that it is there to put the files in.
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(paths.firmware), ec);
    Console::setFirmwareFolders({paths.firmware});
}

void AppMain::setup(MainContext& m) {
    m_ = &m;
    soundDir_ = m.paths.sound;
    m.ui.setSoundfonts(m.paths.soundfont, m.paths.sound + "soundfonts/");
    m.ui.setSynth(synth_.get(), soundDir_);
}

void AppMain::allNotesOff() {
    if (m_->app.console && m_->app.console->synth()) m_->app.console->synth()->allNotesOff();
}

void AppMain::watchSongs(std::vector<SynthMusic::Song> songs, bool keepAsked) {
    std::vector<SongWatcher::Request> requests;
    for (const SynthMusic::Song& s : songs)
        if (!s.request.empty()) requests.push_back({s.request, s.number});
    synth_->setSongs(std::move(songs), keepAsked);
    if (m_->app.console) songWatcher_.attach(m_->app.console.get(), synth_.get(), requests);
}

void AppMain::applySynth() {
    const Settings& settings = m_->settings;
    App& app = m_->app;
    // The SoundFont is loaded when the synthesizer is on, or the instrument editor is
    // open (it plays the songs with it, game or no game).
    if (settings.synth || m_->ui.editorOpen()) {
        const std::string path = settings.soundfont.empty() ? m_->paths.soundfont : settings.soundfont;
        if (path != synthFont_) {
            synthFont_ = path;
            std::vector<uint8_t> data;
            synthFontOk_ = readFile(path, data) && synth_->loadSoundfont(data);
            if (!synthFontOk_)
                m_->ui.notify(trf("No se pudo abrir el SoundFont %s: la música suena original.", path.c_str()));
        }
    }
    synth_->setEffects(settings.reverb, settings.chorus);
    if (!app.console) return;
    const bool on = settings.synth && app.console->synthSupported() && synthFontOk_;
    // Another game: what was learnt of the last one's instruments does not apply.
    if (app.console.get() != synthConsole_ || app.romPath != synthGame_) {
        // (The console watched before is gone: nothing to take the watcher off.)
        if (app.console.get() != synthConsole_) songWatcher_.release();
        synthConsole_ = app.console.get();
        synthGame_ = app.romPath;
        synth_->forget();
        synth_->setAssignments(InstrumentEditor::load(soundDir_, app.romPath));
        watchSongs(InstrumentEditor::songsFor(soundDir_, app.romPath), false);
        synth_->setSoundEffects(InstrumentEditor::effectsFor(soundDir_, app.romPath), settings.effectsOriginal);
    }
    if (synth_->soundEffectsOriginal() != settings.effectsOriginal)
        synth_->setSoundEffects(InstrumentEditor::effectsFor(soundDir_, app.romPath), settings.effectsOriginal);
    app.console->setSynth(on ? synth_.get() : nullptr, settings.chipMix);
}

void AppMain::loadNotices() {
    Console& console = *m_->app.console;
    if (!console.loadNotice().empty()) m_->ui.notify(console.loadNotice());
}

void AppMain::loadFailed(const std::string& path) {
    const std::string& lastError = m_->app.lastError;
    // A console not emulated yet says so; anything else is the file's fault.
    const bool notYet = isGameFile(path) && !emulated(detectSystem(path));
    if (!m_->capturing)
        m_->ui.showError(notYet ? lastError
                                : trf("No se pudo abrir %s:\n%s\n\nplumnin abre cartuchos de Game Boy, Game Boy Color, "
                                      "NES y Super Nintendo. Si el archivo es uno de ellos, puede estar "
                                      "incompleto o dañado: prueba a copiarlo otra vez.",
                                      fileName(path).c_str(), lastError.c_str()));
    if (!notYet) m_->settings.removeRecent(path);
}

void AppMain::openWindows(const AppCommandLine& cmd, int stage) {
    if (stage == 2 && cmd.instruments) m_->ui.openInstrumentEditor();
}

void AppMain::stateLoaded(bool loaded) {
    App& app = m_->app;
    if (!loaded || !app.console || !app.console->synth()) return;
    app.console->synth()->allNotesOff();  // what was playing is not what the state plays
    // The synthesizer's choices and the sound effects as they are on disk now: Sacar
    // canciones may have found others since the game was opened.
    synth_->setAssignments(InstrumentEditor::load(soundDir_, app.romPath));
    watchSongs(InstrumentEditor::songsFor(soundDir_, app.romPath), false);
    synth_->setSoundEffects(InstrumentEditor::effectsFor(soundDir_, app.romPath), m_->settings.effectsOriginal);
}

void AppMain::cheatsFor(const std::string& path) {
    const App& app = m_->app;
    cheatsSystem_ = app.romLoaded && path == app.romPath ? app.console->system() : detectSystem(path);
}

void AppMain::netStarted() {
    m_->app.console->setPadType(0, PadType::Pad);
    m_->app.console->setPadType(1, PadType::Pad);
}

void AppMain::everyTurn(bool canOverclock, int overclock) {
    const Settings& settings = m_->settings;
    if (Console* console = m_->app.console.get()) {
        console->setUnlimitedSprites(settings.unlimitedSprites);
        console->setWidescreen(settings.wideLeft, settings.wideRight);
        if (canOverclock) console->setOverclock(overclock);
    }
    applySynth();
}

void AppMain::fillUiState(UiState& uiState, bool, bool) {
    uiState.synthOn = m_->app.console && m_->app.console->synth();
    uiState.console = m_->app.console.get();
    uiState.cheatsSystem = cheatsSystem_;
}

// A song of the instrument editor: the game waits while it plays.
bool AppMain::playMusic(double, Audio& audio, FramePacer& pacer, double& turnTarget) {
    Ui& ui = m_->ui;
    if (!ui.songPlaying()) return false;
    const Settings& settings = m_->settings;
    turnTarget = 60;
    for (int n = pacer.framesForTick(1.0); n > 0; --n) {
        std::vector<int16_t> out;
        ui.playSong(1.0 / 60.0, audio.rate(), out);
        audio.push(out, settings.volume, settings.mute || m_->capturing);
    }
    return true;
}

}  // namespace plumnin
