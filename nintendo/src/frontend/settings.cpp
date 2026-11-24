// plumnin's own part of plumnin.ini: the synthesizer and the second port. The rest is
// common/src/frontend/settings_ini.cpp.
#include "settings.h"

#include "core/ini_names.h"

#include <algorithm>
#include <cstdlib>

namespace plumnin {

namespace {

using plum::ini::flag;
using plum::ini::kRegionNames;
using plum::ini::lower;
using plum::ini::yes;

// A port's type as plumnin.ini writes it.
const char* padIni(PadType t) { return t == PadType::None ? "none" : "pad"; }

}  // namespace

void Settings::parseKey(const std::string& section, const std::string& key, const std::string& value) {
    Settings& s = *this;
    if (section == "audio") {
        if (key == "synth") s.synth = yes(value);
        else if (key == "soundfont") s.soundfont = value;
        else if (key == "synth_chip") s.chipMix = std::clamp(std::atoi(value.c_str()), 0, 100);
        else if (key == "reverb") s.reverb = std::clamp(std::atoi(value.c_str()), 0, 100);
        else if (key == "chorus") s.chorus = std::clamp(std::atoi(value.c_str()), 0, 100);
        else if (key == "effects") s.effectsOriginal = value != "synthesizer";
    } else if (section == "console") {
        if (key == "region") {
            for (int i = 0; i < 4; ++i)
                if (iniName(lower(value)) == kRegionNames[i]) s.region = static_cast<Region>(i);
        } else if (key == "pad2") {  // pad 1 is always there
            s.padType[1] = lower(value) == "none" ? PadType::None : PadType::Pad;
        } else if (key == "overclock") {
            s.overclock = std::clamp(std::atoi(value.c_str()), 100, kMaxOverclock);
        }
    }
}

void Settings::writeAudio(std::ostream& out) const {
    out << "# synth: the music as General MIDI instruments on a SoundFont (soundfont, a .sf2; empty for\n"
        << "# the included GeneralUser GS). synth_chip: how loud the chip's own music stays under it,\n"
        << "# reverb and chorus: 0 to 100. effects: original (the sound effects keep the chip's sound)\n"
        << "# or synthesizer.\n"
        << "synth = " << flag(synth) << "\n"
        << "soundfont = " << soundfont << "\n"
        << "synth_chip = " << chipMix << "\n"
        << "reverb = " << reverb << "\n"
        << "chorus = " << chorus << "\n"
        << "effects = " << (effectsOriginal ? "original" : "synthesizer") << "\n";
}

void Settings::writeConsole(std::ostream& out) const {
    out << "# region: auto (the cartridge's), japan, usa or europe. pad2: pad, or none for nothing plugged in.\n"
        << "region = " << kRegionNames[static_cast<int>(region)] << "\n"
        << "pad2 = " << padIni(padType[1]) << "\n"
        << "# overclock: the console's processor speed in percent, from 100 (the console's) to 400.\n"
        << "# It removes slowdown in some games; a few may run too fast or fail.\n"
        << "overclock = " << overclock << "\n";
}

void Settings::writeAfterLibrary(std::ostream&) const {}

}  // namespace plumnin
