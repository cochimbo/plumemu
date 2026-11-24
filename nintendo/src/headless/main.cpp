// plumnin-headless: the emulator without a window, sound card or GPU, for tests, CI and
// reproducing bugs. It runs a cartridge for a number of frames, feeds the
// pads from an input script and writes what was asked for: screenshots, the sound as
// WAV, a hash of every frame, savestates and memory dumps. It can also check the
// result against expected hashes and say on which frame a run first went different.
//
// Run it without arguments (or with --help) for the options and the script format.

#include <cctype>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <set>
#include <string>
#include <vector>

#include "core/cheat_list.h"
#include "console.h"
#include "songs.h"
#include "song_export.h"
#include "miniz.h"
#include "net/udp.h"
#include "core/netplay.h"
#include "headless/headless_run.h"
#include "headless/headless_tools.h"
#include "core/recording.h"
#include "system.h"
#include "test_console.h"

using namespace plumnin;
using namespace plum::headless;

namespace {

const char* kUsage = R"(usage: plumnin-headless <game|--test-rom> [options]

  game                  cartridge (.gb .gbc .nes .sfc .smc, or inside a .zip)
  --test-rom            no game: the built-in test pattern (two planes, a sprite the
                        pad moves, a tone while A is held), for checking everything
                        around the consoles
  --system gb|gbc|nes|snes   with --test-rom: the console it looks like (default nes)

Running
  --frames N            frames to run (default 600); frames are numbered from 0
  --region jp|us|eu     force the console's region (default: what the game says)
  --no-pad2             nothing plugged into the second port (the script's p2 buttons do nothing)
  --no-sprite-limit     draw every sprite on a line (the option against flicker)
  --widescreen W:H      draw the picture W:H wide (16:9, 16:10) instead of 4:3
  --widescreen L,R      or with L more columns on the left and R on the right (up to 56)
  --overclock PERCENT   run the processor at 100-400% of its speed (default 100)
  --load-state FILE     start from this savestate (after loading the game)
  --firmware DIR        where the firmware of a cartridge's chip is (dsp1b.rom...), besides
                        the game's folder
  --cheat CODE          the console's Game Genie or Pro Action Replay code, several
                        joined with + (can repeat)

Input
  --input FILE          input script (see below)
  --do "LINE"           one script line given on the command line (can repeat)

Netplay (two plumnin over UDP, as the window's Red menu does)
  --netplay-host PORT   wait for the other player on this UDP port; this side is pad 1
  --netplay-join ADDR   join the host at ADDR (name or IP, :PORT, default 7845); pad 2
  --input-delay N       frames between a press and its effect (host's choice, default 2)
  --net-lag MS          hold each packet this side sends for MS milliseconds, and
  --net-loss PERCENT    drop this many of them, to try a slow or bad connection
  With either, the script's buttons for this side's pad drive it (p1.* on the host,
  p2.* on the guest) and only button lines are allowed. A button on frame N takes
  effect on frame N as long as N is not below the input delay (those frames have no
  input), so both sides' scripts together, run without netplay, give the same
  frames: compare them with --hash-log and --expect-log. The game starts from the
  host's console when the guest arrives; the log has the frames both agreed on.

Output
  --png FILE            the last frame as PNG
  --layers PREFIX       also PREFIX_planeB.png, PREFIX_planeA.png, PREFIX_sprites.png
  --png-every N         a PNG every N frames, as frame_000123.png in --out-dir
  --out-dir DIR         where screenshots without a name go (default: .)
  --wav FILE            the whole run's sound (44100 Hz stereo)
  --mp3 FILE            a .vgm, .vgz or .spc exported as the instrument editor's Exportar a
                        MP3 does: round its loop twice and faded out (with --synth, through it)
  --synth SF2           the music through the synthesizer, on this SoundFont
  --synth-chip PERCENT  how loud the chip's own sound stays under it (default 0)
  --effects FILE        the sound effects' instruments (the effects.txt of --rip-songs):
                        they keep the chip's sound under --synth
  --rip-songs DIR       Sacar canciones: every song of the game (Game Boy, Color and NES,
                        each to DIR as a .vgz with its loop; Super Nintendo as a .spc), then exit
  --vgm FILE            the run's music as a VGM (gzip-compressed, .vgz), as the
                        instrument editor records it (Game Boy, Color and NES)
  --spc FILE            the music as it is after the last frame, as an SPC (Super Nintendo)
  --gif FILE            the run as an animated GIF, as plumnin's Grabar GIF makes it
  --avi FILE            the run as a video with its sound (Motion JPEG and PCM)
                        (both at the console's own size, and not with netplay)
  --save-state FILE     a savestate after the last frame
  --dump-ram FILE       the memory RetroAchievements reads (the console's RAM) after
                        the last frame
  --hash-log FILE       one line per frame: frame, picture hash, sound hash
  --json FILE           the summary as JSON (- for standard output)
  --quiet               no summary on standard output

Checks (exit code 3 when one fails)
  --expect-hash HEX     the last frame's picture hash must be HEX
  --expect-audio HEX    the hash of the whole run's sound must be HEX
  --expect-log FILE     every frame must match this --hash-log; the first frame that
                        does not is reported

Input scripts: one command per line, # starts a comment.
  FRAMES BUTTONS...     hold the buttons during FRAMES; BUTTONS are up down left right
                        a b x y l r select start, for pad 2 written p2.a, p2.start...
  FRAMES screenshot [FILE]   PNG after the frame (frame_000123.png in --out-dir)
  FRAMES savestate FILE      savestate after the frame
  FRAMES loadstate FILE      load a savestate before the frame
  FRAMES dump-ram FILE       work RAM after the frame
  FRAMES hash                print the frame's hash
  FRAMES reset               press the reset button before the frame
  FRAMES power               switch off and on again before the frame
  FRAMES quit                stop after this frame
FRAMES is N (one frame), N-M (M included) or N+K (K frames from N). Lines with
buttons add up; a button is held only on the frames its lines cover.

  # wait for the title, press Start, walk right while jumping
  120+4    start
  300-420  right
  330+10   right a
  420      screenshot level.png
  600      quit

Exit codes: 0 fine, 1 could not load or write something, 2 bad arguments or
script, 3 a check failed.
)";

}  // namespace

// The instruments the synthesizer heard and what each sounds as.
static void printInstruments(SynthMusic& synth) {
    for (const auto& [fp, seen] : synth.seen()) {
        const SynthMusic::Assignment a = synth.assignmentFor(fp);
        const SynthMusic::Traits& t = seen.traits;
        std::printf("instrument %08x: %d notes %d-%d voices %02x | %s%s %.2fs bright %.2f fund %.2f odd %.2f rough %.0f Hz strike %.5f"
                    " | held %d decayed %d swelled %d -> ",
                    fp, seen.notes, seen.low, seen.high, seen.voices, t.pitched ? "pitched" : "unpitched",
                    t.looped ? " looped" : "", t.seconds, t.brightness, t.fundamental, t.odd, t.roughHz, t.strike, seen.heard.held,
                    seen.heard.decayed, seen.heard.swelled);
        if (a.original) std::printf("original\n");
        else if (a.drum >= 0) std::printf("drum %d %s\n", a.drum, SynthMusic::drumName(a.drum) ? SynthMusic::drumName(a.drum) : "");
        else std::printf("%d %s%s\n", a.program, SynthMusic::programName(a.program), a.octave ? " (octave shifted)" : "");
    }
}


namespace {

// What only plumnin knows about the game being run: its console, the synthesizer and the
// music it records.
class NintendoGame : public RunGame {
public:
    std::unique_ptr<Console> console_;
    SynthMusic synth;
    VgmWriter vgm;
    std::string vgmOut, spcOut, synthFont;

    ConsoleBase& console() override { return *console_; }
    const std::vector<int16_t>& samples() override { return console_->audio().samples(); }
    bool writeRam(const std::string& path) override {
        std::vector<uint8_t> ram;
        for (const MemoryRegion& m : console_->memoryMap()) ram.insert(ram.end(), m.data, m.data + m.size);
        return writeFile(path, ram.data(), ram.size());
    }
    bool writeOutputs() override {
        bool ok = true;
        if (!vgmOut.empty()) {
            console_->recordMusic(nullptr);
            const std::vector<uint8_t> file = vgm.finish({});
            if (!writeFile(vgmOut, file.data(), file.size())) ok = false;
        }
        if (!spcOut.empty()) {
            const std::vector<uint8_t> file = console_->musicSnapshot("", 0);
            if (file.empty()) std::fprintf(stderr, "--spc: this console's music cannot be saved as an SPC\n");
            if (file.empty() || !writeFile(spcOut, file.data(), file.size())) ok = false;
        }
        return ok;
    }
    std::string title() override { return console_->title(); }
    std::string system() override { return systemKey(console_->system()); }
    bool pal() override { return console_->effectiveRegion() == Region::EuropePal; }
    // With --synth: the instruments it heard and what each sounds as.
    void printDetails() override {
        if (!synthFont.empty()) printInstruments(synth);
    }
};

}  // namespace

int main(int argc, char** argv) {
    RunOptions o;
    ConsoleSystem testSystem = ConsoleSystem::Nes;
    long synthChip = 0;
    std::string synthFont, ripDir, spcOut, effectsFile;
    int wideLeft = 0, wideRight = 0;
    Region region = Region::Auto;

    if (argc < 2) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    for (Args args(argc, argv); args.next();) {
        const std::string& a = args.arg();
        if (a == "--help" || a == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        } else if (o.parse(args)) {
        } else if (a == "--region") {
            const std::string r = args.value();
            if (r == "jp" || r == "japan") region = Region::JapanNtsc;
            else if (r == "us" || r == "usa") region = Region::UsaNtsc;
            else if (r == "eu" || r == "europe" || r == "pal") region = Region::EuropePal;
            else args.setMissing();
        } else if (a == "--pal") region = Region::EuropePal;
        else if (a == "--system") {
            const std::string v = args.value();
            if (v == "gb") testSystem = ConsoleSystem::GameBoy;
            else if (v == "gbc") testSystem = ConsoleSystem::GameBoyColor;
            else if (v == "nes") testSystem = ConsoleSystem::Nes;
            else if (v == "snes") testSystem = ConsoleSystem::Snes;
            else args.setMissing();
        }
        else if (a == "--widescreen") {
            const std::string v = args.value();
            int w = 0, h = 0;
            if (std::sscanf(v.c_str(), "%d:%d", &w, &h) == 2 && w > 0 && h > 0) wideLeft = wideRight = wideColumnsFor(w, h);
            else if (std::sscanf(v.c_str(), "%d,%d", &wideLeft, &wideRight) != 2 || wideLeft < 0 || wideRight < 0) args.setMissing();
        }
        else if (a == "--firmware") Console::setFirmwareFolders({args.value()});
        else if (a == "--synth") synthFont = args.value();
        else if (a == "--synth-chip") args.number(&synthChip);
        else if (a == "--effects") effectsFile = args.value();
        else if (a == "--rip-songs") ripDir = args.value();
        else if (a == "--spc") spcOut = args.value();
        else if (a.size() > 2 && a.compare(0, 2, "--") == 0 && a != "--test-rom") {
            std::fprintf(stderr, "unknown option %s (see --help)\n", a.c_str());
            return 2;
        } else if (!o.game.empty()) {
            std::fprintf(stderr, "only one game at a time (%s and %s)\n", o.game.c_str(), a.c_str());
            return 2;
        } else o.game = a;
        if (args.missing()) {
            std::fprintf(stderr, "%s needs a valid value (see --help)\n", a.c_str());
            return 2;
        }
    }
    const std::string &game = o.game, &wav = o.wav, &mp3Out = o.mp3Out;
    const long frames = o.frames;
    const bool quiet = o.quiet;
    if (game.empty()) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    if (const int code = o.loadInput(); code >= 0) return code;
    const bool netplay = o.netplay();

    // A VGM instead of a game: it plays for as long as --frames at 60 a second, to --wav.
    auto endsWith = [](const std::string& s, const char* end) {
        const size_t n = std::strlen(end);
        return s.size() >= n && std::equal(s.end() - static_cast<long>(n), s.end(), end,
                                           [](char a, char b) { return std::tolower(a) == b; });
    };
    if (endsWith(game, ".vgm") || endsWith(game, ".vgz") || endsWith(game, ".spc")) {
        std::vector<uint8_t> data;
        VgmFile file;
        std::string error;
        if (!readFile(game, data) || !file.parse(std::move(data), &error)) {
            std::fprintf(stderr, "cannot load %s: %s\n", game.c_str(), error.empty() ? "cannot read it" : error.c_str());
            return 1;
        }
        VgmPlayer player;
        player.load(std::move(file));
        SynthMusic synth;
        if (!synthFont.empty()) {
            if (!readFile(synthFont, data) || !synth.loadSoundfont(data)) {
                std::fprintf(stderr, "cannot load SoundFont %s\n", synthFont.c_str());
                return 1;
            }
            player.setSynth(&synth, static_cast<int>(synthChip));
        }
        std::vector<int16_t> out;
        if (!mp3Out.empty()) {
            // As the instrument editor exports it: the rules by the whole song first.
            if (!synthFont.empty()) {
                synth.forget();
                synth.learn(VgmPlayer::instruments(player.file()));
            }
            const VgmFile song = player.file();
            if (!renderSong(song, synthFont.empty() ? nullptr : &synth, synthFont.empty() ? 100 : static_cast<int>(synthChip),
                            SongRender{}, &out)) return 1;
            const std::vector<uint8_t> mp3 = encodeMp3(out, 44100, 192, {song.title, song.game, "", 0});
            if (mp3.empty() || !writeFile(mp3Out, mp3.data(), mp3.size())) return 1;
            if (!wav.empty() && !writeWav(wav, out, 44100)) return 1;
            if (!quiet) std::printf("%s: %.1f s to %s\n", game.c_str(), out.size() / 2 / 44100.0, mp3Out.c_str());
            return 0;
        }
        for (long i = 0; i < frames && !player.finished(); ++i) player.play(1.0 / 60.0, 44100, out);
        if (!wav.empty() && !writeWav(wav, out, 44100)) return 1;
        if (!synthFont.empty() && !quiet) printInstruments(synth);
        if (!quiet)
            std::printf("%s: %.1f s of %.1f (loop %.1f s), \"%s\" from \"%s\"\n", game.c_str(), player.position(),
                        player.file().seconds(), player.file().loopSamples / 44100.0, player.file().title.c_str(),
                        player.file().game.c_str());
        return 0;
    }

    if (!ripDir.empty()) {
        std::vector<uint8_t> rom;
        std::string name, error, report;
        if (!readRomFile(game, rom, &name, &error)) {
            std::fprintf(stderr, "cannot load %s: %s\n", game.c_str(), error.c_str());
            return 1;
        }
        SongRipper::Options options;
        SongRipper::Progress progress;
        std::vector<SongRipper::Song> songs;
        const auto t0 = std::chrono::steady_clock::now();
        std::set<uint32_t> effects;
        const bool ok = SongRipper::run(rom, options, &progress, &songs, &error, &report, &effects);
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fputs(report.c_str(), stdout);
        if (!ok) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::filesystem::create_directories(ripDir);
        for (const SongRipper::Song& s : songs) {
            char file[64];
            // (An SPC by its order: the Super Nintendo's numbers repeat, one set of songs for
            // each part of the game.)
            if (s.spc) std::snprintf(file, sizeof file, "/song %02d.spc", static_cast<int>(&s - songs.data()) + 1);
            else std::snprintf(file, sizeof file, "/song %03d.vgz", s.number);
            if (!writeFile(ripDir + file, s.vgz.data(), s.vgz.size())) return 1;
            std::printf("%d: %.1f s%s%s\n", s.number, s.seconds,
                        s.loopSeconds > 0 ? (", loop " + std::to_string(s.loopSeconds) + " s").c_str() : ", ends",
                        s.request.empty() ? "" : (", asked with " + s.request).c_str());
        }
        // The sound effects' instruments, as the instrument editor keeps them.
        if (!effects.empty()) {
            std::string text;
            char line[16];
            for (uint32_t fp : effects) {
                std::snprintf(line, sizeof line, "%08x\n", fp);
                text += line;
            }
            if (!writeFile(ripDir + "/effects.txt", text.data(), text.size())) return 1;
            std::printf("%zu sound effect instruments\n", effects.size());
        }
        std::printf("%zu songs in %.1f s\n", songs.size(), took);
        return 0;
    }

    // The console.
    NintendoGame run;
    run.vgmOut = o.vgmOut;
    run.spcOut = spcOut;
    run.synthFont = synthFont;
    std::unique_ptr<Console>& console = run.console_;
    if (game == "--test-rom") {
        console = std::make_unique<TestConsole>(testSystem);
        console->loadRomData({0});
    } else {
        std::string error;
        console = openGame(game, &error);
        if (!console) {
            std::fprintf(stderr, "cannot load %s: %s\n", game.c_str(), error.c_str());
            return 1;
        }
        if (!console->loadNotice().empty()) std::fprintf(stderr, "%s\n", console->loadNotice().c_str());
    }
    Console& md = *console;
    md.setRegion(region);
    if (o.noPad2 && !netplay) md.setPadType(1, PadType::None);  // the guest plays on pad 2
    md.setUnlimitedSprites(o.noSpriteLimit);
    md.setWidescreen(wideLeft, wideRight);
    md.setOverclock(static_cast<int>(o.overclock));
    if (!o.cheats.empty()) {
        std::vector<CheatPatch> patches;
        for (const std::string& code : o.cheats) {
            std::vector<CheatPatch> p;
            std::string error;
            if (!decodeCheatCodes(code, md.cheatDecoder(), &p, &error)) {
                std::fprintf(stderr, "bad cheat %s: %s\n", code.c_str(), error.c_str());
                return 2;
            }
            patches.insert(patches.end(), p.begin(), p.end());
        }
        md.setCheats(patches);
    }
    if (!o.stateIn.empty() && !loadStateFile(md, o.stateIn)) return 1;

    md.setSampleRate(44100);
    SynthMusic& synth = run.synth;
    if (!synthFont.empty()) {
        std::vector<uint8_t> data;
        if (!readFile(synthFont, data) || !synth.loadSoundfont(data)) {
            std::fprintf(stderr, "cannot load SoundFont %s\n", synthFont.c_str());
            return 1;
        }
        md.setSynth(&synth, static_cast<int>(synthChip));
        if (!effectsFile.empty()) {
            std::vector<uint8_t> text;
            if (!readFile(effectsFile, text)) {
                std::fprintf(stderr, "cannot read %s\n", effectsFile.c_str());
                return 1;
            }
            std::set<uint32_t> effects;
            const std::string lines(text.begin(), text.end());
            for (size_t at = 0; at < lines.size();) {
                size_t end = lines.find('\n', at);
                if (end == std::string::npos) end = lines.size();
                if (const uint32_t fp = static_cast<uint32_t>(std::strtoul(lines.substr(at, end - at).c_str(), nullptr, 16)))
                    effects.insert(fp);
                at = end + 1;
            }
            synth.setSoundEffects(std::move(effects), true);
        }
    }
    md.audio().clearSamples();
    if (!o.vgmOut.empty() && !md.recordMusic(&run.vgm)) {
        std::fprintf(stderr, "--vgm: this console's music cannot be recorded\n");
        return 2;
    }
    return plum::headless::run(run, o);
}
