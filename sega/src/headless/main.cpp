// plumsg-headless: the emulator without a window, sound card or GPU, for tests, CI and
// reproducing bugs. It runs a cartridge or a disc for a number of frames, feeds the
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

#include "cheats.h"
#include "megadrive.h"
#include "miniz.h"
#include "net/udp.h"
#include "core/netplay.h"
#include "headless/headless_run.h"
#include "headless/headless_tools.h"
#include "core/recording.h"
#include "pad_test_rom.h"
#include "sms_test_rom.h"
#include "song_export.h"
#include "song_extract.h"
#include "system.h"
#include "test_rom.h"

using namespace plumsg;
using namespace plum::headless;

namespace {

const char* kUsage = R"(usage: plumsg-headless <game|--test-rom|--pad-test-rom|--sms-test-rom|--gg-test-rom> [options]

  game                  cartridge (.md .gen .bin .smd .32x .zip) or disc (.cue .chd .iso)
  --play-vgm FILE       no game: play this .vgm or .vgz for --frames sixtieths of a
                        second, for --wav and --roland, and list its instruments
  --mp3 FILE            with --play-vgm: export it as the instrument editor's Exportar a
                        MP3 does (twice round its loop and faded out; with --roland,
                        through it)
  --extract-songs DIR   take the game's songs out of its ROM (sound drivers like
                        Sonic's, Streets of Rage 2's, EA's or GEMS), one .vgz each in DIR,
                        saying what it finds
  --test-rom            the built-in test program (planes, sprites, scroll, a tone)
  --pad-test-rom        the built-in pad test: the backdrop shows the pressed buttons
  --sms-test-rom        the built-in Master System test (tiles, a sprite, scroll, a tone)
  --gg-test-rom         the same for the Game Gear
  --two-pad-test-rom    the built-in two-pad test: every frame depends on all that both
                        pads pressed before it (for checking netplay)

Running
  --frames N            frames to run (default 600); frames are numbered from 0
  --region jp|us|eu     force the console's region (default: what the game says)
  --six-button          6-button pads instead of 3-button ones
  --no-pad2             nothing plugged into the second port (the script's p2 buttons do nothing)
  --no-sprite-limit     draw every sprite on a line (the option against flicker)
  --widescreen W:H      draw the picture W:H wide (16:9, 16:10) instead of 4:3
  --widescreen L,R      or with L more columns on the left and R on the right (up to 56)
  --overclock PERCENT   run the 68000 at 100-400% of its speed (default 100)
  --bios FILE           Mega CD BIOS; its region comes from its header (else: all)
  --bios-dir DIR        look for Mega CD BIOS files in DIR and its subfolders
  --load-state FILE     start from this savestate (after loading the game)
  --cheat CODE          Game Genie or Action Replay code, several joined with + (can repeat)

Input
  --input FILE          input script (see below)
  --do "LINE"           one script line given on the command line (can repeat)

Netplay (two plumsg over UDP, as the window's Red menu does)
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
  --gif FILE            the run as an animated GIF, as plumsg's Grabar GIF makes it
  --avi FILE            the run as a video with its sound (Motion JPEG and PCM)
                        (both at the console's own size, and not with netplay)
  --roland SF2          play the FM and PSG music as General MIDI on this SoundFont,
                        as the Roland sound does (the DAC stays as it is)
  --midi-log FILE       with --roland: the notes it played, one event per line
  --effects LIST        with --roland: these FM instruments (hex fingerprints, as
                        --extract-songs lists them) are sound effects, left original
  --vgm FILE            the run's YM2612 and PSG music as a VGM (.vgz: compressed)
  --save-state FILE     a savestate after the last frame
  --dump-ram FILE       the 68000's work RAM (64 KB) after the last frame (a Master
                        System game: its 8 KB of RAM)
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
                        a b c x y z start mode, for pad 2 written p2.a, p2.start...
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
  330+10   right c
  420      screenshot level.png
  600      quit

Exit codes: 0 fine, 1 could not load or write something, 2 bad arguments or
script, 3 a check failed.
)";

}  // namespace

// --extract-songs: the instrument editor's Sacar canciones, without a window.
int extractSongs(const std::string& game, const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    SongExtractor extractor;
    // (PLUMSG_RIP_THREADS=n: on n threads, to compare; as many as there are cores without it.)
    if (const char* n = std::getenv("PLUMSG_RIP_THREADS")) extractor.setThreads(std::atoi(n));
    extractor.setLog([](const std::string& s) {
        std::printf("%s\n", s.c_str());
        std::fflush(stdout);
    });
    const auto started = std::chrono::steady_clock::now();
    std::string error;
    std::string last;
    std::string name = game.substr(game.find_last_of("/\\") == std::string::npos ? 0 : game.find_last_of("/\\") + 1);
    name = name.substr(0, name.find_last_of('.'));  // the tag says the game's name, not its path
    const bool ok = extractor.run(
        game, name,
        [&](const SongExtractor::Progress& p) {
            if (p.doing.compare(0, 8, "Probando") != 0 && p.doing != last) std::printf("%s\n", p.doing.c_str());
            last = p.doing;
            return true;
        },
        [&](const SongExtractor::Song& song) {
            char name[64];
            std::snprintf(name, sizeof name, "/Song %02X.vgz", song.number);
            std::ofstream out(dir + name, std::ios::binary);
            out.write(reinterpret_cast<const char*>(song.vgz.data()), static_cast<std::streamsize>(song.vgz.size()));
            if (!out) std::fprintf(stderr, "cannot write %s%s\n", dir.c_str(), name);
        },
        &error);
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("%.0f s\n", took);
    if (!extractor.effectInstruments().empty()) {
        std::printf("Efectos de sonido:");
        for (uint32_t fp : extractor.effectInstruments()) std::printf(" %08x", fp);
        std::printf("\n");
    }
    if (!ok) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    return 0;
}

// --play-vgm: the instrument editor's player, without a window.
int playVgmFile(const std::string& path, long frames, const std::string& wav, const std::string& roland,
                const std::string& midiLog, const std::string& mp3Out) {
    std::vector<uint8_t> bytes;
    VgmFile file;
    std::string error;
    if (!readFile(path, bytes) || !file.parse(std::move(bytes), &error)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.empty() ? "cannot read it" : error.c_str());
        return 1;
    }
    RolandSound rolandSound;
    std::vector<std::string> midiEvents;
    VgmPlayer player;
    player.load(file);
    player.setRoland(&rolandSound);
    player.setRolandOutput(!roland.empty());
    if (!roland.empty()) {
        std::vector<uint8_t> font;
        if (!readFile(roland, font) || !rolandSound.loadSoundfont(font)) {
            std::fprintf(stderr, "%s is not a SoundFont\n", roland.c_str());
            return 1;
        }
    }
    player.analyse();
    std::printf("title: %s\ngame: %s\nlength: %.1f s%s\ninstruments:\n", file.title.c_str(), file.game.c_str(),
                file.seconds(), file.loop ? " (loops)" : "");
    for (const auto& [fp, seen] : rolandSound.seen()) {
        const RolandSound::Assignment a = rolandSound.assignmentFor(fp);
        std::printf("  %08x %s voices %03x notes %d (%d-%d): %s\n", fp,
                    seen.kind == RolandSound::Kind::Fm        ? "FM"
                    : seen.kind == RolandSound::Kind::PsgTone ? "PSG"
                    : seen.kind == RolandSound::Kind::Rhythm  ? "rhythm"
                                                              : "noise",
                    seen.channels, seen.notes, seen.low, seen.high,
                    a.drum >= 0 ? RolandSound::drumName(a.drum) : RolandSound::programName(a.program));
    }
    if (!midiLog.empty()) rolandSound.setLog(&midiEvents);
    std::vector<int16_t> sound;
    if (!mp3Out.empty()) {
        if (!renderSong(file, roland.empty() ? nullptr : &rolandSound, nullptr, nullptr, SongRender{}, &sound)) return 1;
        const std::vector<uint8_t> mp3 = encodeMp3(sound, 44100, 192, {file.title, file.game, std::string(), 0});
        if (mp3.empty() || !writeFile(mp3Out, mp3.data(), mp3.size())) return 1;
        if (!wav.empty() && !writeWav(wav, sound, 44100)) return 1;
        std::printf("%.1f s to %s\n", static_cast<double>(sound.size() / 2) / 44100.0, mp3Out.c_str());
        return 0;
    }
    for (long i = 0; i < frames && !player.finished(); ++i) player.play(1.0 / 60, 44100, sound);
    bool ioError = !wav.empty() && !writeWav(wav, sound, 44100);
    if (!midiLog.empty()) {
        std::ofstream out(midiLog);
        for (const std::string& e : midiEvents) out << e << "\n";
        if (!out) ioError = true;
    }
    return ioError ? 1 : 0;
}

namespace {

// What only plumsg knows about the game being run: the Mega Drive, its Roland sound and
// its VGM, and the processors and names it reports.
class SegaGame : public RunGame {
public:
    MegaDrive md;
    RolandSound rolandSound;
    std::vector<std::string> midiEvents;
    VgmWriter vgmWriter;
    std::string vgmOut, midiLog;

    ConsoleBase& console() override { return md; }
    const std::vector<int16_t>& samples() override { return md.audio().samples(); }
    void beforeFrame() override { rolandSound.waitForTimbres(); }  // the same sound on every run
    void stateLoaded() override { rolandSound.allNotesOff(); }
    bool writeRam(const std::string& path) override {
        return md.masterSystem() ? writeFile(path, md.z80Ram().data(), md.z80Ram().size())
                                 : writeFile(path, md.workRam().data(), md.workRam().size());
    }
    // As wide as H40 (an H32 picture is stretched to it, as on a TV).
    int recordingWidth(const VideoFrame&) override { return kMdMaxWidth; }
    bool writeOutputs() override {
        bool ok = true;
        if (!vgmOut.empty()) {
            const bool compress = vgmOut.size() > 4 && vgmOut.compare(vgmOut.size() - 4, 4, ".vgz") == 0;
            const std::vector<uint8_t> file = vgmWriter.finish(md.masterClocks(), md.cartridge().header().title, compress);
            std::ofstream out(vgmOut, std::ios::binary);
            out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
            if (!out) ok = false;
        }
        if (!midiLog.empty()) {
            std::ofstream out(midiLog);
            for (const std::string& e : midiEvents) out << e << "\n";
            if (!out) ok = false;
        }
        return ok;
    }
    std::string title() override { return md.cartridge().header().title; }
    std::string system() override {
        return md.hasMsuMd()      ? (md.is32x() ? "msu-32x" : "msu-md")
               : md.isMegaCd32x() ? "megacd32x"
               : md.isMegaCd()    ? "megacd"
               : md.is32x()       ? "32x"
               : md.isGameGear()  ? "gamegear"
               : md.isMasterSystem() ? "mastersystem"
                                     : "megadrive";
    }
    bool pal() override { return md.pal(); }
    uint32_t pc() { return md.isMasterSystem() ? md.z80().pc : md.cpu().pc; }
    void printDetails() override {
        std::printf("pc: %06x\n", pc());
        if (md.isMegaCd()) std::printf("sub pc: %06x\n", md.megaCd()->sub().pc);
        if (md.is32x()) std::printf("sh-2 pc: %08x %08x\n", md.mars()->master().pc(), md.mars()->slave().pc());
    }
    void jsonDetails(std::ostream& j) override {
        j << ",\n  \"pc\": \"" << hex8(pc()) << "\"";
        if (md.isMegaCd()) j << ",\n  \"sub_pc\": \"" << hex8(md.megaCd()->sub().pc) << "\"";
        if (md.is32x())
            j << ",\n  \"master_pc\": \"" << hex8(md.mars()->master().pc()) << "\",\n  \"slave_pc\": \""
              << hex8(md.mars()->slave().pc()) << "\"";
    }
};

}  // namespace

int main(int argc, char** argv) {
    RunOptions o;
    std::string effectsList, playVgm, roland, midiLog, biosDir, extractDir;
    std::vector<std::string> bios;
    int wideLeft = 0, wideRight = 0;
    bool sixButton = false;
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
        else if (a == "--six-button") sixButton = true;
        else if (a == "--widescreen") {
            const std::string v = args.value();
            int w = 0, h = 0;
            if (std::sscanf(v.c_str(), "%d:%d", &w, &h) == 2 && w > 0 && h > 0) wideLeft = wideRight = VdpMd::wideColumnsFor(w, h);
            else if (std::sscanf(v.c_str(), "%d,%d", &wideLeft, &wideRight) != 2 || wideLeft < 0 || wideRight < 0) args.setMissing();
        }
        else if (a == "--bios") bios.push_back(args.value());
        else if (a == "--bios-dir") biosDir = args.value();
        else if (a == "--roland") roland = args.value();
        else if (a == "--midi-log") midiLog = args.value();
        else if (a == "--effects") effectsList = args.value();
        else if (a == "--play-vgm") playVgm = args.value();
        else if (a == "--extract-songs") extractDir = args.value();
        else if (a.size() > 2 && a.compare(0, 2, "--") == 0 && a != "--test-rom" && a != "--pad-test-rom" &&
                 a != "--two-pad-test-rom" && a != "--sms-test-rom" && a != "--gg-test-rom") {
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
    if (!playVgm.empty()) return playVgmFile(playVgm, o.frames, o.wav, roland, midiLog, o.mp3Out);
    const std::string& game = o.game;
    if (game.empty()) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    if (!extractDir.empty()) return extractSongs(game, extractDir);
    if (const int code = o.loadInput(); code >= 0) return code;
    const bool netplay = o.netplay();

    // The console.
    SegaGame run;
    run.vgmOut = o.vgmOut;
    run.midiLog = midiLog;
    MegaDrive& md = run.md;
    md.setRegion(region);
    for (int p = 0; p < 2; ++p) md.setPadType(p, sixButton ? PadType::SixButton : PadType::ThreeButton);
    if (o.noPad2 && !netplay) md.setPadType(1, PadType::None);  // the guest plays on pad 2
    md.vdp().setUnlimitedSprites(o.noSpriteLimit);
    md.vdp().setWidescreen(wideLeft, wideRight);
    md.setOverclock(static_cast<int>(o.overclock));
    static const Region kRegions[3] = {Region::JapanNtsc, Region::UsaNtsc, Region::EuropePal};
    if (!biosDir.empty()) {
        const auto found = findCdBios({biosDir});
        for (int r = 0; r < 3; ++r) {
            std::vector<uint8_t> data;
            if (!found[r].empty() && readFile(found[r], data)) md.setCdBios(kRegions[r], data);
        }
    }
    for (const std::string& path : bios) {
        std::vector<uint8_t> data;
        if (!readFile(path, data)) {
            std::fprintf(stderr, "cannot read BIOS %s\n", path.c_str());
            return 1;
        }
        const BiosInfo info = identifyCdBios(data);
        if (!info.valid) std::fprintf(stderr, "warning: %s: %s\n", path.c_str(), info.problem.c_str());
        if (info.region == Region::Auto)
            for (Region r : kRegions) md.setCdBios(r, data);
        else
            md.setCdBios(info.region, data);
    }
    if (game == "--test-rom") md.loadRomData(testrom::build());
    else if (game == "--pad-test-rom") md.loadRomData(testrom::buildPadTest());
    else if (game == "--two-pad-test-rom") md.loadRomData(testrom::buildTwoPadTest());
    else if (game == "--sms-test-rom") md.loadRomData(smstestrom::build());
    else if (game == "--gg-test-rom") md.loadRomData(smstestrom::build(true));
    else {
        std::string error;
        if (!md.loadRom(game, &error)) {
            std::fprintf(stderr, "cannot load %s: %s\n", game.c_str(), error.c_str());
            return 1;
        }
        if (!md.loadNotice().empty()) std::fprintf(stderr, "%s\n", md.loadNotice().c_str());
    }
    if (!o.cheats.empty()) {
        std::vector<CheatPatch> patches;
        for (const std::string& code : o.cheats) {
            std::vector<CheatPatch> p;
            std::string error;
            if (!decodeCheatCodes(code, &p, &error, md.isMasterSystem())) {
                std::fprintf(stderr, "bad cheat %s: %s\n", code.c_str(), error.c_str());
                return 2;
            }
            patches.insert(patches.end(), p.begin(), p.end());
        }
        md.setCheats(patches);
    }
    if (!o.stateIn.empty() && !loadStateFile(md, o.stateIn)) return 1;

    RolandSound& rolandSound = run.rolandSound;
    if (!roland.empty()) {
        std::vector<uint8_t> font;
        if (!readFile(roland, font) || !rolandSound.loadSoundfont(font)) {
            std::fprintf(stderr, "%s is not a SoundFont\n", roland.c_str());
            return 1;
        }
        if (!midiLog.empty()) rolandSound.setLog(&run.midiEvents);
        std::set<uint32_t> effects;
        for (size_t at = 0; at < effectsList.size();) {
            char* stop = nullptr;
            const unsigned long fp = std::strtoul(effectsList.c_str() + at, &stop, 16);
            if (stop == effectsList.c_str() + at) break;
            effects.insert(static_cast<uint32_t>(fp));
            at = static_cast<size_t>(stop - effectsList.c_str());
            while (at < effectsList.size() && (effectsList[at] == ',' || effectsList[at] == ' ')) ++at;
        }
        rolandSound.setSoundEffects(std::move(effects), true);
        md.setRoland(&rolandSound, 0);
    }
    if (!o.vgmOut.empty()) md.setVgmWriter(&run.vgmWriter);

    md.setSampleRate(44100);
    md.audio().clearSamples();
    return plum::headless::run(run, o);
}
