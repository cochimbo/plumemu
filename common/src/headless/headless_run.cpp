#include "headless/headless_run.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#include "core/netplay.h"
#include "core/recording.h"
#include "net/udp.h"

namespace plum {
namespace headless {

std::string Args::value() {
    if (i_ + 1 < argc_) return argv_[++i_];
    missing_ = true;
    return "";
}

void Args::number(long* out) {
    const std::string s = value();
    char* end = nullptr;
    *out = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end != 0 || *out < 0) missing_ = true;
}

void Args::hash(uint32_t* out) {
    const std::string s = value();
    char* end = nullptr;
    *out = static_cast<uint32_t>(std::strtoul(s.c_str(), &end, 16));
    if (s.empty() || *end != 0) missing_ = true;
}

bool RunOptions::parse(Args& args) {
    const std::string& a = args.arg();
    if (a == "--frames") args.number(&frames);
    else if (a == "--no-pad2") noPad2 = true;
    else if (a == "--no-sprite-limit") noSpriteLimit = true;
    else if (a == "--overclock") args.number(&overclock);
    else if (a == "--load-state") stateIn = args.value();
    else if (a == "--cheat") cheats.push_back(args.value());
    else if (a == "--input") scripts.push_back(args.value());
    else if (a == "--do") lines.push_back(args.value());
    else if (a == "--png") png = args.value();
    else if (a == "--layers") layers = args.value();
    else if (a == "--png-every") args.number(&pngEvery);
    else if (a == "--out-dir") outDir = args.value();
    else if (a == "--wav") wav = args.value();
    else if (a == "--gif") gifOut = args.value();
    else if (a == "--avi") aviOut = args.value();
    else if (a == "--vgm") vgmOut = args.value();
    else if (a == "--mp3") mp3Out = args.value();
    else if (a == "--save-state") stateOut = args.value();
    else if (a == "--dump-ram") ramOut = args.value();
    else if (a == "--hash-log") hashLog = args.value();
    else if (a == "--json") json = args.value();
    else if (a == "--quiet") quiet = true;
    else if (a == "--expect-hash") args.hash(&expectHash), haveExpectHash = true;
    else if (a == "--expect-audio") args.hash(&expectAudio), haveExpectAudio = true;
    else if (a == "--expect-log") expectLog = args.value();
    else if (a == "--netplay-host") {
        args.number(&netPort);
        if (netPort == 0 || netPort > 65535) args.setMissing();
        netHost = true;
    } else if (a == "--netplay-join") netJoin = args.value();
    else if (a == "--net-lag") args.number(&netLag);
    else if (a == "--net-loss") {
        args.number(&netLoss);
        if (netLoss > 90) args.setMissing();
    } else if (a == "--input-delay") {
        args.number(&inputDelay);
        if (inputDelay > 10) args.setMissing();
    } else return false;
    return true;
}

int RunOptions::loadInput() {
    if (netHost && !netJoin.empty()) {
        std::fprintf(stderr, "--netplay-host and --netplay-join: one side or the other\n");
        return 2;
    }
    for (const std::string& path : scripts) {
        std::ifstream in(path);
        if (!in) {
            std::fprintf(stderr, "cannot read %s\n", path.c_str());
            return 1;
        }
        int n = 0;
        for (std::string line; std::getline(in, line);)
            if (!parseLine(line, ++n, path, commands)) return 2;
    }
    for (size_t i = 0; i < lines.size(); ++i)
        if (!parseLine(lines[i], static_cast<int>(i + 1), "--do", commands)) return 2;

    if (netplay())
        for (const Command& c : commands)
            if (c.kind != Command::Buttons) {
                std::fprintf(stderr, "script line %d: only buttons can be scripted in netplay\n", c.line);
                return 2;
            }

    if (!expectLog.empty() && !readLog(expectLog, expected)) {
        std::fprintf(stderr, "cannot read %s\n", expectLog.c_str());
        return 1;
    }
    return -1;
}

bool loadStateFile(ConsoleBase& console, const std::string& path) {
    std::vector<uint8_t> data;
    std::string error;
    if (!readFile(path, data)) error = "cannot read it";
    else if (console.loadState(data, &error)) return true;
    std::fprintf(stderr, "cannot load state %s: %s\n", path.c_str(), error.c_str());
    return false;
}

int run(RunGame& game, const RunOptions& o) {
    ConsoleBase& md = game.console();
    const long frames = o.frames;
    const bool netplay = o.netplay();
    const std::vector<Command>& commands = o.commands;
    const std::vector<LogLine>& expected = o.expected;

    auto saveState = [&](const std::string& path) {
        std::vector<uint8_t> data;
        if (md.saveState(data) && writeFile(path, data.data(), data.size())) return true;
        std::fprintf(stderr, "cannot write state %s\n", path.c_str());
        return false;
    };
    auto dumpRam = [&](const std::string& path) {
        if (game.writeRam(path)) return true;
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return false;
    };
    auto screenshot = [&](std::string path, long frame) {
        if (path.empty()) {
            char name[32];
            std::snprintf(name, sizeof name, "frame_%06ld.png", frame);
            path = o.outDir + "/" + name;
        }
        const VideoFrame& f = md.frame();
        if (writePng(path, f.rgba.data(), f.width, f.height, VideoFrame::kMaxWidth)) return true;
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return false;
    };

    std::FILE* log = nullptr;
    if (!o.hashLog.empty()) {
        log = std::fopen(o.hashLog.c_str(), "w");
        if (!log) {
            std::fprintf(stderr, "cannot write %s\n", o.hashLog.c_str());
            return 1;
        }
        std::fprintf(log, "# frame picture sound\n");
    }

    constexpr int kRate = 44100;
    std::vector<int16_t> sound;
    uint32_t audioHash = 2166136261u;
    long divergence = -1;
    std::string divergenceWhat;
    size_t nextExpected = 0;
    long ran = 0;
    bool ioError = false;
    // --gif and --avi: started on the first frame, which gives them their size.
    GameRecording gifRecording, aviRecording;
    auto recordVideo = [&](GameRecording& r, const std::string& path, GameRecording::Format format) {
        if (path.empty()) return;
        const VideoFrame& f = md.frame();
        std::string error;
        // As tall as the first frame without interlace.
        const int height = f.interlaced ? f.height / 2 : f.height;
        if (!r.active() && !r.start(path, format, game.recordingWidth(f), height, md.refreshRate(), kRate, &error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            ioError = true;
            return;
        }
        r.addAudio(game.samples().data(), game.samples().size());
        r.addFrame(f.rgba.data(), f.width, f.height, VideoFrame::kMaxWidth);
    };

    // What happens after each frame is final: the log, the expected log, the sound.
    auto record = [&](long frame, uint32_t picture, const std::vector<int16_t>& samples) {
        const uint32_t frameSound = soundHash(2166136261u, samples);
        audioHash = soundHash(audioHash, samples);
        if (!o.wav.empty()) sound.insert(sound.end(), samples.begin(), samples.end());
        if (log) std::fprintf(log, "%ld %08x %08x\n", frame, picture, frameSound);
        while (nextExpected < expected.size() && expected[nextExpected].frame < frame) ++nextExpected;
        if (divergence < 0 && nextExpected < expected.size() && expected[nextExpected].frame == frame) {
            const LogLine& e = expected[nextExpected];
            if (e.picture != picture) {
                divergence = frame;
                divergenceWhat = "picture " + hex8(picture) + ", expected " + hex8(e.picture);
            } else if (e.sound != frameSound) {
                divergence = frame;
                divergenceWhat = "sound " + hex8(frameSound) + ", expected " + hex8(e.sound);
            }
        }
    };

    const auto start = std::chrono::steady_clock::now();
    std::string netError;
    long rollbacks = 0;
    int ping = 0;
    if (netplay) {
        UdpTransport udp;
        bool ok = udp.open(o.netHost ? static_cast<int>(o.netPort) : 0) && (o.netHost || udp.setPeer(o.netJoin, 7845));
        if (!ok) netError = udp.error();
        NetplaySession::Config config;
        config.inputDelay = static_cast<int>(o.inputDelay);
        LaggyTransport link(udp, o.netLag, o.netLoss);
        NetplaySession session(link, o.netHost ? NetplaySession::Role::Host : NetplaySession::Role::Guest, config);
        // A frame can run several times (guessed pads, then the right ones): keep the
        // last run of each, which is final once both sides have its pads.
        std::vector<std::vector<int16_t>> frameSamples(static_cast<size_t>(frames));
        std::vector<uint32_t> framePictures(static_cast<size_t>(frames));
        session.onFrame = [&](uint32_t f, ConsoleBase&) {
            if (f >= static_cast<uint32_t>(frames)) return;
            frameSamples[f] = game.samples();
            framePictures[f] = frameHash(md.frame());
        };
        const int port = o.netHost ? 0 : 1;
        const auto clockStart = std::chrono::steady_clock::now();
        auto nowMs = [&] {
            return static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - clockStart).count());
        };
        uint32_t doneAt = 0;
        while (ok) {
            const uint32_t now = nowMs();
            link.setNow(now);
            session.poll(md, now);
            if (session.phase() == NetplaySession::Phase::Ended) {
                netError = session.endReason();
                break;
            }
            if (session.phase() == NetplaySession::Phase::Waiting && now > 60000) {
                netError = "nobody joined within a minute";
                break;
            }
            bool advanced = false;
            if (session.frame() < static_cast<uint32_t>(frames)) {
                const long n = static_cast<long>(session.frame()) + o.inputDelay;  // the frame this press lands on
                PadState pads[2];
                for (const Command& c : commands)
                    if (n >= c.first && n <= c.last) orPad(pads[0], c.pad[0]), orPad(pads[1], c.pad[1]);
                advanced = session.advance(md, pads[port]);
                rollbacks += session.lastRollback();
                md.clearAudio();
            } else {
                session.catchUp(md);
                if (session.confirmedFrames() >= static_cast<uint32_t>(frames)) {
                    // Linger a moment so the other side gets our last pads too.
                    if (doneAt == 0) doneAt = now;
                    if (now - doneAt > 1000) break;
                }
            }
            if (!advanced) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ping = session.pingMs();
        if (session.desyncFrame() >= 0 && netError.empty())
            netError = "the two consoles went different on frame " + std::to_string(session.desyncFrame());
        session.end();
        if (netError.empty() || session.confirmedFrames() >= static_cast<uint32_t>(frames)) {
            netError.clear();
            // The log and the checks, from each frame's final run. md shows the last one.
            for (long f = 0; f < frames; ++f) {
                record(f, framePictures[f], frameSamples[f]);
                ++ran;
            }
        }
    }
    for (long frame = 0; frame < frames && !netplay; ++frame) {
        bool quit = false;
        PadState pads[2];
        for (const Command& c : commands) {
            if (frame < c.first || frame > c.last) continue;
            switch (c.kind) {
                case Command::Buttons: orPad(pads[0], c.pad[0]), orPad(pads[1], c.pad[1]); break;
                case Command::LoadState:
                    if (!loadStateFile(md, c.file)) ioError = true;
                    game.stateLoaded();
                    break;
                case Command::Reset: md.reset(); break;
                case Command::Power: md.powerCycle(); break;
                default: break;
            }
        }
        md.setPads(pads[0], pads[1]);
        game.beforeFrame();
        md.runFrame();
        ++ran;
        const uint32_t picture = frameHash(md.frame());
        record(frame, picture, game.samples());
        recordVideo(gifRecording, o.gifOut, GameRecording::Format::Gif);
        recordVideo(aviRecording, o.aviOut, GameRecording::Format::Avi);
        md.clearAudio();
        if (o.pngEvery > 0 && frame % o.pngEvery == 0 && !screenshot("", frame)) ioError = true;
        for (const Command& c : commands) {
            if (frame < c.first || frame > c.last) continue;
            switch (c.kind) {
                case Command::Screenshot: if (!screenshot(c.file, frame)) ioError = true; break;
                case Command::SaveState: if (!saveState(c.file)) ioError = true; break;
                case Command::DumpRam: if (!dumpRam(c.file)) ioError = true; break;
                case Command::Hash: std::printf("frame %ld hash %08x\n", frame, picture); break;
                case Command::Quit: quit = true; break;
                default: break;
            }
        }
        if (quit) break;
    }
    if (!netError.empty()) {
        std::fprintf(stderr, "netplay: %s\n", netError.c_str());
        return 1;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (log) std::fclose(log);
    if (!expected.empty() && divergence < 0 && static_cast<long>(expected.back().frame) >= ran) {
        divergence = ran;
        divergenceWhat = "the run ended before the expected log";
    }

    const VideoFrame& f = md.frame();
    const uint32_t lastHash = frameHash(f);
    if (!o.png.empty() && !screenshot(o.png, ran - 1)) ioError = true;
    if (!o.layers.empty()) {
        for (auto [name, layer] : {std::pair<const char*, const uint32_t*>{"_planeB.png", f.planeB.data()},
                                   {"_planeA.png", f.planeA.data()}, {"_sprites.png", f.sprites.data()}})
            if (!writePng(o.layers + name, layer, f.width, f.height, VideoFrame::kMaxWidth)) ioError = true;
    }
    if (!game.writeOutputs()) ioError = true;
    if (!o.wav.empty() && !writeWav(o.wav, sound, kRate)) ioError = true;
    for (GameRecording* r : {&gifRecording, &aviRecording}) {
        std::string error;
        if (!r->finish(&error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            ioError = true;
        }
    }
    if (!o.stateOut.empty() && !saveState(o.stateOut)) ioError = true;
    if (!o.ramOut.empty() && !dumpRam(o.ramOut)) ioError = true;

    std::vector<std::string> failures;
    if (o.haveExpectHash && lastHash != o.expectHash)
        failures.push_back("picture hash " + hex8(lastHash) + ", expected " + hex8(o.expectHash));
    if (o.haveExpectAudio && audioHash != o.expectAudio)
        failures.push_back("sound hash " + hex8(audioHash) + ", expected " + hex8(o.expectAudio));
    if (divergence >= 0) failures.push_back("frame " + std::to_string(divergence) + ": " + divergenceWhat);

    const std::string title = game.title(), system = game.system();
    const bool pal = game.pal();
    const double fps = seconds > 0 ? ran / seconds : 0;
    if (!o.quiet) {
        std::printf("title: %s\nsystem: %s  region: %s\nframes: %ld  size: %dx%d  hash: %08x\n", title.c_str(),
                    system.c_str(), pal ? "pal" : "ntsc", ran, f.width, f.height, lastHash);
        std::printf("audio hash: %08x\n", audioHash);
        game.printDetails();
        std::printf("speed: %.0f fps (%.1fx)\n", fps, fps / md.refreshRate());
        if (netplay) std::printf("netplay: %ld frames run again, ping %d ms\n", rollbacks, ping);
        for (const std::string& s : failures) std::printf("FAILED: %s\n", s.c_str());
    }
    if (!o.json.empty()) {
        std::ostringstream j;
        j << "{\n  \"title\": " << jsonString(title) << ",\n  \"system\": \"" << system
          << "\",\n  \"pal\": " << (pal ? "true" : "false") << ",\n  \"frames\": " << ran
          << ",\n  \"width\": " << f.width << ",\n  \"height\": " << f.height << ",\n  \"hash\": \"" << hex8(lastHash)
          << "\",\n  \"audio_hash\": \"" << hex8(audioHash) << "\"";
        game.jsonDetails(j);
        j << ",\n  \"fps\": " << static_cast<long>(fps) << ",\n  \"divergence\": " << divergence;
        if (netplay) j << ",\n  \"netplay_rollback_frames\": " << rollbacks << ",\n  \"netplay_ping_ms\": " << ping;
        j << ",\n  \"failures\": [";
        for (size_t i = 0; i < failures.size(); ++i) j << (i ? ", " : "") << jsonString(failures[i]);
        j << "]\n}\n";
        if (o.json == "-") std::fputs(j.str().c_str(), stdout);
        else if (std::ofstream out(o.json); !(out << j.str())) ioError = true;
    }
    if (ioError) return 1;
    return failures.empty() ? 0 : 3;
}

}  // namespace headless
}  // namespace plum
