#include "console.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

#include "core/i18n.h"
#include "miniz.h"

namespace plumnin {

namespace {

std::mutex firmwareMutex;
std::vector<std::string> firmwareFolders;

}  // namespace

const Console::StateFormat Console::kStateFormat{0x534E4C50 /* "PLNS" */, 1, 64u << 20, 2};

std::string Console::stateError(StateError e) const {
    switch (e) {
    case StateError::NotAState: return tr("el archivo no es un estado de plumnin");
    case StateError::Newer: return tr("el estado es de una versión más nueva de plumnin");
    case StateError::OtherGame: return tr("el estado es de otra ROM");
    default: return tr("el estado está dañado");
    }
}

void Console::saveStateHeader(StateWriter& w) {
    uint8_t sys = static_cast<uint8_t>(system());
    uint8_t region = static_cast<uint8_t>(effectiveRegion());
    w(sys, region);
}

std::string Console::checkStateHeader(const uint8_t* header) const {
    if (header[0] != static_cast<uint8_t>(system())) return tr("el estado es de otra consola");
    return {};
}

void Console::saveAppBlocks(StateBlocks& blocks) {
    blocks.add("PADS", [&](StateWriter& w) { w(padTypes_); });
}

bool Console::loadAppBlock(uint32_t id, StateReader& r) {
    if (id != fourcc("PADS")) return false;
    r(padTypes_);
    return true;
}

bool Console::loadRom(const std::string& path, std::string* error) {
    std::vector<uint8_t> rom;
    if (!readRomFile(path, rom, nullptr, error)) return false;
    return loadRomFrom(std::move(rom), path, error);
}

bool Console::loadRomData(std::vector<uint8_t> rom, std::string* error) { return loadRomFrom(std::move(rom), {}, error); }

void Console::setFirmwareFolders(std::vector<std::string> folders) {
    std::lock_guard<std::mutex> lock(firmwareMutex);
    firmwareFolders = std::move(folders);
}

std::vector<uint8_t> Console::findFirmware(const std::string& name) const {
    namespace fs = std::filesystem;
    std::vector<fs::path> dirs;
    if (!romPath_.empty()) {
        const fs::path game = fs::u8path(romPath_).parent_path();
        dirs.push_back(game);
        dirs.push_back(game / "firmware");
    }
    {
        std::lock_guard<std::mutex> lock(firmwareMutex);
        for (const std::string& f : firmwareFolders) dirs.push_back(fs::u8path(f));
    }
    for (const fs::path& d : dirs) {
        std::error_code ec;
        const fs::path p = d / fs::u8path(name);
        if (!fs::is_regular_file(p, ec)) continue;
        std::ifstream in(p, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!data.empty()) return data;
    }
    return {};
}

bool Console::loadRomFrom(std::vector<uint8_t> rom, const std::string& path, std::string* error) {
    romPath_ = path;
    loadNotice_.clear();
    if (rom.empty()) {
        if (error) *error = tr("el archivo está vacío");
        return false;
    }
    if (!insert(rom, error)) return false;
    romCrc_ = static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, rom.data(), rom.size()));
    rom_ = std::move(rom);
    frames_ = 0;
    forgetLoadedThumbnail();
    audio_.clearSamples();
    return true;
}

void Console::setRegion(Region r) {
    if (r == region_) return;
    region_ = r;
    regionChanged();
}

uint32_t Console::checksum() {
    // FNV-1a of the memory the achievements read: where games keep what matters.
    uint32_t h = 2166136261u;
    for (const MemoryRegion& m : memoryMap())
        for (size_t i = 0; i < m.size; ++i) h = (h ^ m.data[i]) * 16777619u;
    return h;
}

std::vector<uint8_t> Console::achievementsHashData() {
    const std::vector<uint8_t>& data = rom();
    if (data.empty()) return {};
    // What RetroAchievements hashes: the cartridge without the headers that dumps add,
    // the 16 bytes of the NES's iNES header and the 512 of a SNES copier's.
    size_t skip = 0;
    if (system() == ConsoleSystem::Nes && data.size() > 16 && data[0] == 'N' && data[1] == 'E' && data[2] == 'S' &&
        data[3] == 0x1A)
        skip = 16;
    if (system() == ConsoleSystem::Snes && data.size() % 1024 == 512) skip = 512;
    return std::vector<uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(skip), data.end());
}

uint32_t Console::readAchievementsMemory(uint32_t address, uint8_t* buffer, uint32_t count) {
    // The console lays its memory out in RetroAchievements' order; the gaps read as nothing.
    const std::vector<MemoryRegion> map = memoryMap();
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t a = address + i;
        bool found = false;
        for (const MemoryRegion& m : map)
            if (a >= m.address && a - m.address < m.size) {
                buffer[i] = m.data[a - m.address];
                found = true;
                break;
            }
        if (!found) return i;
    }
    return count;
}

bool Console::stateThumbnail(const std::vector<uint8_t>& state, std::vector<uint32_t>& pixels, int* width,
                             int* height) {
    return plum::ConsoleBase::stateThumbnail(kStateFormat, state, pixels, width, height);
}

bool Console::recordMusic(VgmWriter* recorder) {
    if (recorder && !musicRecordable()) return false;
    if (recorder_) endMusicRecording(*recorder_);
    recorder_ = recorder;
    if (recorder_) beginMusicRecording(*recorder_);
    return true;
}

void Console::setSynth(SynthMusic* synth, int chipPercent) {
    if (!synthSupported()) synth = nullptr;
    if (synth != synth_) {
        if (synth_) synth_->allNotesOff();
        if (synth) synth->allNotesOff();
    }
    synth_ = synth;
    synthPercent_ = std::clamp(chipPercent, 0, 100);
    chipSet_ = false;
    updateChipVoices();
}

void Console::updateChipVoices() {
    const uint32_t full = synth_ ? synth_->chipVoices() : ~0u;
    const uint32_t silent = synth_ ? synth_->silentVoices() : 0u;
    if (chipSet_ && full == chipFull_ && silent == chipSilent_) return;
    chipSet_ = true;
    chipFull_ = full;
    chipSilent_ = silent;
    setChipVoices(full, silent, synth_ ? synthPercent_ : 100);
}

void Console::beginRunAhead() {
    // The frames ahead are not heard, nor are their notes played.
    aheadSynth_ = synth_;
    synth_ = nullptr;
    aheadRecorder_ = recorder_;
    recorder_ = nullptr;
}

void Console::endRunAhead() {
    synth_ = aheadSynth_;
    recorder_ = aheadRecorder_;
    // The frames to come are run again for real, and heard then.
    audio_.clearSamples();
}

}  // namespace plumnin
