#include "console_base.h"

#include <array>

#include "miniz.h"

namespace plum {

namespace {

constexpr int kMaxHeaderBytes = 8;

// Unpacks a savestate file into its body; false if it is not one of `format`, with why.
bool unpackState(const ConsoleBase::StateFormat& format, const std::vector<uint8_t>& in, std::vector<uint8_t>& body,
                 int* why) {
    enum { NotAState, Newer, Damaged };
    if (in.size() < 12) return *why = NotAState, false;
    StateReader header(in.data(), 12);
    uint32_t magic = 0, version = 0, rawSize = 0;
    header(magic, version, rawSize);
    if (magic != format.magic) return *why = NotAState, false;
    if (version > format.version) return *why = Newer, false;
    if (rawSize == 0 || rawSize > format.maxSize) return *why = Damaged, false;
    body.resize(rawSize);
    mz_ulong size = rawSize;
    if (mz_uncompress(body.data(), &size, in.data() + 12, static_cast<mz_ulong>(in.size() - 12)) != MZ_OK ||
        size != rawSize)
        return *why = Damaged, false;
    return true;
}

// The body's header: the game's CRC, the console's own bytes and the frame count.
bool readHeader(const ConsoleBase::StateFormat& format, StateReader& r, uint32_t& crc,
                std::array<uint8_t, kMaxHeaderBytes>& own, uint64_t& frames) {
    r(crc);
    if (format.headerBytes > kMaxHeaderBytes || !r.raw(own.data(), static_cast<size_t>(format.headerBytes)))
        return false;
    r(frames);
    return r.ok();
}

}  // namespace

void ConsoleBase::StateBlocks::raw(const char (&tag)[5], const std::vector<uint8_t>& data) {
    uint32_t id = fourcc(tag), size = static_cast<uint32_t>(data.size());
    body_(id, size);
    body_.raw(data.data(), data.size());
}

bool ConsoleBase::saveState(std::vector<uint8_t>& out, bool quick) {
    const StateFormat& format = stateFormat();
    StateWriter body;
    uint32_t crc = stateCrc();
    body(crc);
    saveStateHeader(body);
    body(frames_);
    StateBlocks blocks(body);
    saveAppBlocks(blocks);
    saveBlocks(blocks);

    if (quick) {
        // No thumbnail: nobody looks at a rewind snapshot.
    } else if (!loadedThumbnail_.empty() && thumbnailFrame_ == frames_) {
        // Nothing has run since a state was loaded: the picture on screen is still the
        // one from before, so keep the loaded state's thumbnail.
        blocks.raw("THMB", loadedThumbnail_);
    } else {
        // Thumbnail: the 2D picture at half size, without the widescreen columns.
        const VideoFrame& f = frame();
        const int step = f.interlaced ? 2 : 1;
        const int scale = f.consoleWidth() > 320 ? 4 : 2;  // the SNES's 512 columns at a quarter
        uint16_t tw = static_cast<uint16_t>(f.consoleWidth() / scale), th = static_cast<uint16_t>(f.height / step / 2);
        StateWriter w;
        w(tw, th);
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x) {
                const uint32_t c = f.pixel(f.wideLeft + x * scale, y * 2 * step) & 0xFFFFFF;
                const uint8_t rgb[3] = {static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8),
                                        static_cast<uint8_t>(c)};
                w.raw(rgb, 3);
            }
        blocks.raw("THMB", w.data());
    }

    mz_ulong packedSize = mz_compressBound(static_cast<mz_ulong>(body.data().size()));
    std::vector<uint8_t> packed(packedSize);
    if (mz_compress2(packed.data(), &packedSize, body.data().data(), static_cast<mz_ulong>(body.data().size()),
                     quick ? 1 : 6) != MZ_OK)
        return false;
    packed.resize(packedSize);
    StateWriter file;
    uint32_t magic = format.magic, version = format.version, rawSize = static_cast<uint32_t>(body.data().size());
    file(magic, version, rawSize);
    file.raw(packed.data(), packed.size());
    out = std::move(file.data());
    return true;
}

bool ConsoleBase::loadState(const std::vector<uint8_t>& in, std::string* error) {
    auto fail = [&](StateError e) {
        if (error) *error = stateError(e);
        return false;
    };
    const StateFormat& format = stateFormat();
    std::vector<uint8_t> body;
    int why = 0;
    if (!unpackState(format, in, body, &why))
        return fail(why == 0 ? StateError::NotAState : why == 1 ? StateError::Newer : StateError::Damaged);

    StateReader r(body.data(), body.size());
    uint32_t crc = 0;
    std::array<uint8_t, kMaxHeaderBytes> header{};
    uint64_t frames = 0;
    if (!readHeader(format, r, crc, header, frames)) return fail(StateError::Damaged);
    const std::string problem = checkStateHeader(header.data());
    if (!problem.empty()) {
        if (error) *error = problem;
        return false;
    }
    if (crc != stateCrc()) return fail(StateError::OtherGame);

    // Keep the current state so a bad block cannot leave the console half loaded.
    std::vector<uint8_t> backup;
    saveState(backup, true);
    beforeLoadBlocks(header.data());

    bool ok = true;
    std::vector<uint8_t> thumbnail;
    while (ok && !r.atEnd()) {
        uint32_t id = 0, size = 0;
        r(id, size);
        if (!r.ok() || size > r.remaining()) {
            ok = false;
            break;
        }
        std::vector<uint8_t> data(size);
        if (!r.raw(data.data(), size)) {
            ok = false;
            break;
        }
        StateReader b(data.data(), data.size());
        if (id == fourcc("THMB")) thumbnail = std::move(data);
        else if (!loadAppBlock(id, b)) ok = loadBlock(id, b);
        if (ok && !b.ok()) ok = false;
    }
    if (!ok) {
        std::vector<uint8_t> restore;
        if (unpackState(format, backup, restore, &why)) {
            // Back to how it was, through the same code (the backup is a state of this
            // very console). The quick backup has no thumbnail: keep the one there was.
            std::vector<uint8_t> keptThumbnail = loadedThumbnail_;
            const uint64_t keptFrame = thumbnailFrame_;
            std::string ignored;
            loadState(backup, &ignored);
            loadedThumbnail_ = std::move(keptThumbnail);
            thumbnailFrame_ = keptFrame;
        }
        return fail(StateError::BrokenBlock);
    }
    frames_ = frames;
    afterLoad();
    loadedThumbnail_ = std::move(thumbnail);
    thumbnailFrame_ = frames_;
    return true;
}

bool ConsoleBase::stateThumbnail(const StateFormat& format, const std::vector<uint8_t>& state,
                             std::vector<uint32_t>& pixels, int* width, int* height) {
    std::vector<uint8_t> body;
    int why = 0;
    if (!unpackState(format, state, body, &why)) return false;
    StateReader r(body.data(), body.size());
    uint32_t crc = 0;
    std::array<uint8_t, kMaxHeaderBytes> header{};
    uint64_t frames = 0;
    if (!readHeader(format, r, crc, header, frames)) return false;
    while (r.ok() && !r.atEnd()) {
        uint32_t id = 0, size = 0;
        r(id, size);
        if (!r.ok() || size > r.remaining()) return false;
        std::vector<uint8_t> data(size);
        if (!r.raw(data.data(), size)) return false;
        if (id != fourcc("THMB")) continue;
        StateReader t(data.data(), data.size());
        uint16_t w = 0, h = 0;
        t(w, h);
        if (!t.ok() || static_cast<size_t>(w) * h * 3 != t.remaining() || w == 0 || h == 0) return false;
        pixels.resize(static_cast<size_t>(w) * h);
        for (uint32_t& p : pixels) {
            uint8_t rgb[3];
            t.raw(rgb, 3);
            p = 0xFF000000u | static_cast<uint32_t>(rgb[0]) << 16 | static_cast<uint32_t>(rgb[1]) << 8 | rgb[2];
        }
        *width = w;
        *height = h;
        return true;
    }
    return false;
}

bool ConsoleBase::runAhead(int frames) {
    if (frames <= 0) return true;
    if (!saveState(aheadState_, true)) return false;
    beginRunAhead();
    const bool drawing = drawing_;
    for (int i = 1; i <= frames; ++i) {
        drawing_ = i == frames;  // only the last picture is seen
        runFrame();
    }
    drawing_ = drawing;
    endRunAhead();
    // The picture is not part of the state, so loading it keeps the one just drawn.
    std::string error;
    return loadState(aheadState_, &error);
}

}  // namespace plum
