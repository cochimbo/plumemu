#include "core/gm_sound.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "core/i18n.h"

// TinySoundFont's code, once for both synthesizers (core/gm_synth.h).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#include "tsf.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace plum {
namespace gm {

namespace {

const char* const kPrograms[128] = {
    N_("Piano de cola"), N_("Piano brillante"), N_("Piano eléctrico de cola"), N_("Piano de cantina"), N_("Piano eléctrico 1"),
    N_("Piano eléctrico 2"), N_("Clavecín"), N_("Clavinet"), N_("Celesta"), N_("Glockenspiel"), N_("Caja de música"), N_("Vibráfono"),
    N_("Marimba"), N_("Xilófono"), N_("Campanas tubulares"), N_("Dulcémele"), N_("Órgano de barras"), N_("Órgano de percusión"),
    N_("Órgano de rock"), N_("Órgano de iglesia"), N_("Armonio"), N_("Acordeón"), N_("Armónica"), N_("Bandoneón"), N_("Guitarra de nailon"),
    N_("Guitarra de acero"), N_("Guitarra eléctrica de jazz"), N_("Guitarra eléctrica limpia"), N_("Guitarra eléctrica apagada"),
    N_("Guitarra saturada"), N_("Guitarra distorsionada"), N_("Armónicos de guitarra"), N_("Bajo acústico"), N_("Bajo eléctrico (dedo)"),
    N_("Bajo eléctrico (púa)"), N_("Bajo sin trastes"), N_("Bajo slap 1"), N_("Bajo slap 2"), N_("Bajo sintetizado 1"),
    N_("Bajo sintetizado 2"), N_("Violín"), N_("Viola"), N_("Violonchelo"), N_("Contrabajo"), N_("Cuerdas en trémolo"),
    N_("Cuerdas en pizzicato"), N_("Arpa"), N_("Timbales"), N_("Cuerdas 1"), N_("Cuerdas 2"), N_("Cuerdas sintetizadas 1"),
    N_("Cuerdas sintetizadas 2"), N_("Coro \"aah\""), N_("Voz \"ooh\""), N_("Voz sintetizada"), N_("Golpe de orquesta"), N_("Trompeta"),
    N_("Trombón"), N_("Tuba"), N_("Trompeta con sordina"), N_("Trompa"), N_("Sección de metales"), N_("Metales sintetizados 1"),
    N_("Metales sintetizados 2"), N_("Saxo soprano"), N_("Saxo alto"), N_("Saxo tenor"), N_("Saxo barítono"), N_("Oboe"), N_("Corno inglés"),
    N_("Fagot"), N_("Clarinete"), N_("Flautín"), N_("Flauta"), N_("Flauta dulce"), N_("Flauta de pan"), N_("Botella soplada"), N_("Shakuhachi"),
    N_("Silbido"), N_("Ocarina"), N_("Solista cuadrada"), N_("Solista diente de sierra"), N_("Solista calíope"), N_("Solista chiff"),
    N_("Solista charango"), N_("Solista voz"), N_("Solista quintas"), N_("Solista bajo y melodía"), N_("Fondo new age"), N_("Fondo cálido"),
    N_("Fondo polisintetizador"), N_("Fondo coro"), N_("Fondo arco"), N_("Fondo metálico"), N_("Fondo halo"), N_("Fondo barrido"),
    N_("Efecto lluvia"), N_("Efecto banda sonora"), N_("Efecto cristal"), N_("Efecto atmósfera"), N_("Efecto brillo"),
    N_("Efecto duendes"), N_("Efecto ecos"), N_("Efecto ciencia ficción"), N_("Sitar"), N_("Banjo"), N_("Shamisen"), N_("Koto"), N_("Kalimba"),
    N_("Gaita"), N_("Violín celta"), N_("Shanai"), N_("Campanilla"), N_("Agogó"), N_("Tambores metálicos"), N_("Caja china"), N_("Taiko"),
    N_("Tom melódico"), N_("Tambor sintetizado"), N_("Platillo al revés"), N_("Traste de guitarra"), N_("Respiración"), N_("Olas"),
    N_("Pájaros"), N_("Teléfono"), N_("Helicóptero"), N_("Aplausos"), N_("Disparo")};

const char* const kDrumNames[47] = {
    N_("Bombo acústico"), N_("Bombo"), N_("Golpe de aro"), N_("Caja"), N_("Palmada"), N_("Caja eléctrica"), N_("Tom grave de suelo"),
    N_("Charles cerrado"), N_("Tom agudo de suelo"), N_("Charles con pedal"), N_("Tom grave"), N_("Charles abierto"),
    N_("Tom medio grave"), N_("Tom medio agudo"), N_("Platillo crash 1"), N_("Tom agudo"), N_("Platillo ride 1"), N_("Platillo chino"),
    N_("Campana del ride"), N_("Pandereta"), N_("Platillo splash"), N_("Cencerro"), N_("Platillo crash 2"), N_("Vibraslap"),
    N_("Platillo ride 2"), N_("Bongó agudo"), N_("Bongó grave"), N_("Conga aguda apagada"), N_("Conga aguda abierta"), N_("Conga grave"),
    N_("Timbal agudo"), N_("Timbal grave"), N_("Agogó agudo"), N_("Agogó grave"), N_("Cabasa"), N_("Maracas"), N_("Silbato corto"),
    N_("Silbato largo"), N_("Güiro corto"), N_("Güiro largo"), N_("Claves"), N_("Caja china aguda"), N_("Caja china grave"),
    N_("Cuíca apagada"), N_("Cuíca abierta"), N_("Triángulo apagado"), N_("Triángulo abierto")};

}  // namespace

const char* programName(int program) { return program >= 0 && program < 128 ? tr(kPrograms[program]) : ""; }

const char* drumName(int note) { return note >= 35 && note <= 81 ? tr(kDrumNames[note - 35]) : nullptr; }

Assignments parseAssignments(const std::string& text) {
    Assignments out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (const size_t c = line.find(';'); c != std::string::npos) line.resize(c);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        char* stop = nullptr;
        const std::string key = line.substr(0, eq);
        const unsigned long fp = std::strtoul(key.c_str(), &stop, 16);
        if (stop == key.c_str() || fp == 0) continue;
        char word[32] = "";
        int a = 0, b = 0, c = 0;
        const char* value = line.c_str() + eq + 1;
        Assignment as;
        if (std::sscanf(value, " %31s", word) != 1) continue;
        if (std::string(word) == "original") {
            as.original = true;
        } else if (std::string(word) == "bateria") {
            const int n = std::sscanf(value, " %*s %d %d %d", &a, &b, &c);
            if (n < 1 || !drumName(a)) continue;
            as.drum = a;
            if (n >= 3) as.volume = std::clamp(c, 0, 200);
        } else {
            const int n = std::sscanf(value, " %d %d %d", &a, &b, &c);
            if (n < 1 || a < 0 || a > 127) continue;
            as.program = a;
            if (n >= 2) as.octave = std::clamp(b, -3, 3);
            if (n >= 3) as.volume = std::clamp(c, 0, 200);
        }
        out[static_cast<uint32_t>(fp)] = as;
    }
    return out;
}

std::string formatAssignments(const Assignments& assignments, const char* heading, bool withNames) {
    std::string out = heading;
    for (const auto& [fp, a] : assignments) {
        char line[160];
        if (a.original)
            std::snprintf(line, sizeof line, "%08x = original\n", fp);
        else if (a.drum >= 0 && withNames)
            std::snprintf(line, sizeof line, "%08x = bateria %d 0 %d ; %s\n", fp, a.drum, a.volume, drumName(a.drum));
        else if (a.drum >= 0)
            std::snprintf(line, sizeof line, "%08x = bateria %d 0 %d\n", fp, a.drum, a.volume);
        else if (withNames)
            std::snprintf(line, sizeof line, "%08x = %d %d %d ; %s\n", fp, a.program, a.octave, a.volume,
                          programName(a.program));
        else
            std::snprintf(line, sizeof line, "%08x = %d %d %d\n", fp, a.program, a.octave, a.volume);
        out += line;
    }
    return out;
}

void endKeyQuick(tsf* f, int channel, int key) {
    for (int i = 0; i < f->voiceNum; ++i) {
        tsf_voice& v = f->voices[i];
        if (v.playingPreset != -1 && v.playingChannel == channel && v.playingKey == key) tsf_voice_endquick(f, &v);
    }
}

}  // namespace gm
}  // namespace plum
