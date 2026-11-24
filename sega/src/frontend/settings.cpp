// plumsg's own part of plumsg.ini: the Roland sound, the ports and the Mega CD BIOS. The
// rest is common/src/frontend/settings_ini.cpp.
#include "settings.h"

#include "core/i18n.h"
#include "core/ini_names.h"
#include "frontend/game_options.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace plumsg {

namespace {

using plum::ini::flag;
using plum::ini::kRegionNames;
using plum::ini::lower;
using plum::ini::yes;

// A port's type as plumsg.ini writes it.
const char* padIni(PadType t) {
    switch (t) {
        case PadType::ThreeButton: return "3";
        case PadType::None: return "0";
        case PadType::Menacer: return "menacer";
        case PadType::Justifier: return "justifier";
        case PadType::LightPhaser: return "phaser";
        case PadType::Paddle: return "paddle";
        default: return "6";
    }
}

constexpr const char* kBiosKeys[] = {"megacd_japan", "megacd_usa", "megacd_europe"};

}  // namespace

void Settings::parseKey(const std::string& section, const std::string& key, const std::string& value) {
    Settings& s = *this;
    if (section == "audio") {
        if (key == "mode") s.roland = iniName(lower(value)) == "synthesizer" || iniName(lower(value)) == "roland";
        else if (key == "soundfont") s.soundfont = value;
        else if (key == "chip_mix") s.chipMix = std::clamp(std::atoi(value.c_str()), 0, 100);
        else if (key == "reverb") s.reverb = std::clamp(std::atoi(value.c_str()), 0, 100);
        else if (key == "chorus") s.chorus = std::clamp(std::atoi(value.c_str()), 0, 100);
        else if (key == "equalizer") {
            std::array<int, 5> bands{};
            if (std::sscanf(value.c_str(), "%d , %d , %d , %d , %d", &bands[0], &bands[1], &bands[2], &bands[3],
                            &bands[4]) == 5)
                for (size_t i = 0; i < bands.size(); ++i) s.equalizer[i] = std::clamp(bands[i], -12, 12);
        }
        else if (key == "effects") s.effectsOriginal = iniName(lower(value)) == "original";
        else if (key == "compare_sound") s.compareSound = yes(value);
    } else if (section == "console") {
        if (key == "region") {
            for (int i = 0; i < 4; ++i)
                if (iniName(lower(value)) == kRegionNames[i]) s.region = static_cast<Region>(i);
        } else if (key == "pad1" || key == "pad2") {
            PadType t = value == "3" ? PadType::ThreeButton : PadType::SixButton;
            if (lower(value) == "phaser") t = PadType::LightPhaser;
            else if (lower(value) == "paddle") t = PadType::Paddle;
            else if (key == "pad2") {  // pad 1 is always there, and light guns only go in port 2
                if (value == "0") t = PadType::None;
                else if (lower(value) == "menacer") t = PadType::Menacer;
                else if (lower(value) == "justifier") t = PadType::Justifier;
            }
            s.padType[key == "pad1" ? 0 : 1] = t;
        } else if (key == "fm_master_system") {
            s.smsFm = yes(value);
        } else if (key == "overclock") {
            s.overclock = std::clamp(std::atoi(value.c_str()), 100, kMaxOverclock);
        }
    } else if (section == "bios") {
        for (int i = 0; i < 3; ++i)
            if (key == kBiosKeys[i]) s.cdBios[static_cast<size_t>(i)] = value;
    }
}

void Settings::writeAudio(std::ostream& out) const {
    out << "# mode: original (the YM2612 and PSG) or synthesizer (their music on a SoundFont, like an SC-55).\n"
        << "# soundfont: your own .sf2 (empty: the one that comes with plumsg). chip_mix: how much of the\n"
        << "# original sound is heard under the synthesizer, 0 to 100. reverb and chorus: 0 to 100.\n"
        << "mode = " << (roland ? "synthesizer" : "original") << "\n"
        << "soundfont = " << soundfont << "\n"
        << "chip_mix = " << chipMix << "\n"
        << "reverb = " << reverb << "\n"
        << "chorus = " << chorus << "\n"
        << "# equalizer: the dB (-12 to 12) of bass, low mids, mids, high mids and treble.\n"
        << "equalizer = " << equalizer[0] << ", " << equalizer[1] << ", " << equalizer[2] << ", " << equalizer[3]
        << ", " << equalizer[4] << "\n"
        << "# effects: original (sound effects sound as on the console) or synthesizer. Get songs has to\n"
        << "# have been used on the game to know which its effects are.\n"
        << "effects = " << (effectsOriginal ? "original" : "synthesizer") << "\n"
        << "# compare_sound: the instrument the rules give is refined by quietly playing the FM sound and\n"
        << "# those of its SoundFont family, and keeping the closest.\n"
        << "compare_sound = " << flag(compareSound) << "\n";
}

void Settings::writeConsole(std::ostream& out) const {
    out << "# region: auto (the ROM's), japan, usa or europe. pad1/pad2: 3 or 6 buttons; pad2 = 0: nothing plugged in,\n"
        << "# menacer or justifier: that light gun, aimed with the mouse or the left stick. Either one = phaser or\n"
        << "# paddle: the Master System's Light Phaser or paddle (Master System games only).\n"
        << "region = " << kRegionNames[static_cast<int>(region)] << "\n"
        << "pad1 = " << padIni(padType[0]) << "\n"
        << "pad2 = " << padIni(padType[1]) << "\n"
        << "# fm_master_system: yes, the Master System FM unit outside Japan too.\n"
        << "fm_master_system = " << flag(smsFm) << "\n"
        << "# overclock: processor (68000) speed in percent, from 100 (the console's) to 400.\n"
        << "# It removes slowdown in some games; a few may run too fast or fail.\n"
        << "overclock = " << overclock << "\n";
}

void Settings::writeAfterLibrary(std::ostream& out) const {
    out << "\n[bios]\n"
        << "# The Mega CD BIOS for each region (they do not come with plumsg).\n";
    for (int i = 0; i < 3; ++i) out << kBiosKeys[i] << " = " << cdBios[static_cast<size_t>(i)] << "\n";
}

std::vector<GameOption> soundGameOptions() {
    return {
        {"mode", N_("Sintetizador"), N_("Sonido"), [](const Settings& f, Settings& t) { t.roland = f.roland; }},
        {"soundfont", N_("SoundFont"), N_("Sonido"), [](const Settings& f, Settings& t) { t.soundfont = f.soundfont; }},
        {"chip_mix", N_("Sonido original mezclado"), N_("Sonido"), [](const Settings& f, Settings& t) { t.chipMix = f.chipMix; }},
        {"reverb", N_("Reverb"), N_("Sonido"), [](const Settings& f, Settings& t) { t.reverb = f.reverb; }},
        {"chorus", N_("Chorus"), N_("Sonido"), [](const Settings& f, Settings& t) { t.chorus = f.chorus; }},
        {"effects", N_("Efectos de sonido originales"), N_("Sonido"),
         [](const Settings& f, Settings& t) { t.effectsOriginal = f.effectsOriginal; }},
    };
}

std::vector<GameOption> consoleGameOptions() {
    return {
        {"region", N_("Región"), N_("Consola"), [](const Settings& f, Settings& t) { t.region = f.region; }},
        {"pads", N_("Tipo de mando"), N_("Consola"), [](const Settings& f, Settings& t) { t.padType = f.padType; }},
        {"overclock", N_("Overclock"), N_("Consola"), [](const Settings& f, Settings& t) { t.overclock = f.overclock; }},
    };
}

}  // namespace plumsg
