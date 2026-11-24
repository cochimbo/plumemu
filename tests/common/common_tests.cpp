// Tests of what plumsg and plumnin share (common/): the rewind buffer, the frame pacer,
// the 3D view's edge distances, the library's names, the game info's parsers and the
// cache of textures. Each program's own core_tests test the rest with its consoles.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/rewind.h"
#include "frontend/game_info_data.h"
#include "frontend/library_names.h"
#include "frontend/texture_cache.h"
#include "render/planes.h"

using namespace plum;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

void testRewindBuffer() {
    RewindBuffer r;
    r.setLimits(3, 1000);
    for (uint8_t i = 1; i <= 5; ++i) r.push(std::vector<uint8_t>(10, i));
    std::vector<uint8_t> s;
    check(r.size() == 3 && r.bytes() == 30, "rewind: keeps only the newest snapshots");
    check(r.pop(s) && s[0] == 5 && r.pop(s) && s[0] == 4 && r.pop(s) && s[0] == 3 && !r.pop(s) && r.bytes() == 0,
          "rewind: pops newest first, then runs out");
    r.setLimits(100, 25);
    for (uint8_t i = 1; i <= 5; ++i) r.push(std::vector<uint8_t>(10, i));
    check(r.size() == 2 && r.bytes() == 20, "rewind: the byte limit drops the oldest too");
    r.setLimits(0, 1000);
    r.push(std::vector<uint8_t>(10, 9));
    check(r.empty(), "rewind: no frames means off");
}

void testFramePacer() {
    auto total = [](double speed, int ticks, int* most = nullptr) {
        FramePacer p;
        int sum = 0, top = 0;
        for (int i = 0; i < ticks; ++i) {
            const int n = p.framesForTick(speed);
            sum += n;
            top = std::max(top, n);
        }
        if (most) *most = top;
        return sum;
    };
    int most = 0;
    check(total(1.0, 60, &most) == 60 && most == 1, "speed: normal runs one frame per tick");
    check(total(4.0, 60, &most) == 240 && most == 4, "speed: fast forward x4");
    check(total(0.5, 60, &most) == 30 && most == 1, "speed: slow motion at 50% runs every other tick");
    check(total(0.1, 100) == 10, "speed: 10% adds up without losing frames to rounding");
    check(total(2.5, 4) == 10, "speed: fractions carry over between ticks");
}

void testEdgeDistance() {
    constexpr int kW = 64, kH = 48;
    std::vector<uint32_t> plane(kW * kH, 0);
    for (int y = 10; y < 30; ++y)
        for (int x = 10; x < 30; ++x) plane[y * kW + x] = 0xFF123456u;
    markEdgeDistance(plane, kW, kH);
    auto distance = [&](int x, int y) { return kMaxEdgeDistance - (255 - int(plane[y * kW + x] >> 24)) / 16; };
    check(distance(10, 10) == 1 && distance(29, 20) == 1, "edge distance: 1 on the outline");
    check(distance(11, 20) == 2 && distance(20, 12) == 3, "edge distance: grows inwards");
    check(distance(20, 20) == kMaxEdgeDistance, "edge distance: capped in the middle");
    check(plane[0] == 0 && (plane[20 * kW + 20] & 0xFFFFFF) == 0x123456, "edge distance: colours untouched");
}

void testLibraryNames() {
    check(gameTitle("Lion King, The (World)") == "The Lion King", "library: trailing ', The' moves to the front");
    check(gameTitle("Adventures of Batman & Robin, The (USA)") == "The Adventures of Batman & Robin",
          "library: article with an ampersand in the name");
    check(gameTitle("Sonic The Hedgehog 2 (World) (Rev B)") == "Sonic The Hedgehog 2", "library: tags dropped");
    check(gameTitle("Golden Axe III") == "Golden Axe III", "library: name without tags kept");
    check(gameTags("Sonic The Hedgehog 2 (World) (Rev B)") == "World · Rev B", "library: tags listed");
    const auto names = coverNames("Comix Zone (USA) (Sega Smash Pack)");
    check(names.size() > 2 && names[0] == "Comix Zone (USA) (Sega Smash Pack)" && names[1] == "Comix Zone (USA)",
          "library: cover names drop trailing tags one by one");
    check(coverNames("Sonic & Knuckles (World)")[0] == "Sonic _ Knuckles (World)", "library: '&' becomes '_' like libretro");
    const auto bare = coverNames("Golden Axe III");
    check(!bare.empty() && bare[0] == "Golden Axe III (USA)", "library: a bare name tries the usual regions");
    check(urlEncode("Columns (USA, Europe)") == "Columns%20%28USA%2C%20Europe%29", "library: URL encoding");
    check(searchKey("Dr. Robotnik's  Mean-Bean") == "dr robotnik s mean bean", "library: search key");

    const std::string html = "<a href=\"../\">../</a>\n<a href=\"Sonic%20The%20Hedgehog%202%20%28World%29.png\">Sonic...</a>\n"
                             "<a href=\"Ecco%20the%20Dolphin%20%28USA%29.png\">Ecco</a>\n<a href=\"Tools.txt\">x</a>";
    const auto fromHtml = parseCoverList(html);
    check(fromHtml.size() == 2 && fromHtml[0] == "Ecco the Dolphin (USA)" && fromHtml[1] == "Sonic The Hedgehog 2 (World)",
          "library: cover list from libretro's index");
    const std::string json = "{\"tree\":[{\"path\": \"Pok\\u00e9mon (Japan).png\",\"type\":\"blob\"},"
                             "{\"path\":\"Golden Axe II (World).png\"}]}";
    const auto fromJson = parseCoverList(json);
    check(fromJson.size() == 2 && fromJson[0] == "Golden Axe II (World)" && fromJson[1] == "Pok\xc3\xa9mon (Japan)",
          "library: cover list from GitHub's tree");
    const std::vector<std::string> set = {"Golden Axe (World)", "Golden Axe II (World)", "Golden Axe III (Japan)",
                                          "Sonic The Hedgehog (USA, Europe)", "Sonic The Hedgehog 2 (World)",
                                          "Sonic The Hedgehog 3 (USA)", "Streets of Rage 2 (USA)", "Lion King, The (World)"};
    auto similar = similarCoverNames(set, "Golden Axe 2", 3);
    check(!similar.empty() && similar[0] == "Golden Axe II (World)", "library: '2' finds the 'II' cover first");
    similar = similarCoverNames(set, "sonic hedgehod 3", 2);
    check(!similar.empty() && similar[0] == "Sonic The Hedgehog 3 (USA)", "library: a typo still finds the cover");
    similar = similarCoverNames(set, "The Lion King (Europe)", 5);
    check(!similar.empty() && similar[0] == "Lion King, The (World)", "library: ', The' and other regions");
    check(similarCoverNames(set, "Columns", 5).empty(), "library: nothing alike, no suggestions");

    const std::string tree = "{\"tree\":[{\"path\":\"Sonic The Hedgehog (USA, Europe).cht\"},"
                             "{\"path\":\"Sonic The Hedgehog 2 (World) (Rev A).cht\"},{\"path\":\"Sonic The Hedgehog 2 (World).cht\"},"
                             "{\"path\":\"Sonic The Hedgehog 2 (World) (Action Replay).cht\"},{\"path\":\"cover.png\"}]}";
    const auto chts = parseFileList(tree, ".cht");
    check(chts.size() == 4 && std::is_sorted(chts.begin(), chts.end()), "library: cheat lists from GitHub's tree");
    auto lists = cheatFileNames(chts, "Sonic The Hedgehog 2 (World)", 5);
    check(lists.size() == 4 && lists[0] == "Sonic The Hedgehog 2 (World)" &&
              lists[1] == "Sonic The Hedgehog 2 (World) (Action Replay)",
          "library: the cheat list named like the ROM first, then its Action Replay one, then similar ones");
    lists = cheatFileNames(chts, "Sonic The Hedgehog 2 (Europe)", 5);
    check(!lists.empty() && lists[0] == "Sonic The Hedgehog 2 (World)", "library: another region's cheat list");
    check(cheatFileNames(chts, "Columns (World)", 5).empty(), "library: no cheat lists for an unknown game");
}

// The detail panel's data: libretro-database's metadat files and Wikipedia's answers.
void testGameInfo() {
    const std::string genres = "clrmamepro (\n\tname \"Sega - Mega Drive - Genesis\"\n)\n\n"
                               "game (\n\tcomment \"Sonic The Hedgehog 2 (World)\"\n\tgenre \"Platform\"\n"
                               "\trom ( crc 24AB4C3A )\n)\n\n"
                               "game (\n\tcomment \"Sonic The Hedgehog 2 (World) (Rev A)\"\n\tgenre \"Platform\"\n)\n\n"
                               "game (\n\tcomment \"Columns (World)\"\n\trom ( crc 03163D7A )\n)\n";
    const auto entries = parseMetadat(genres, "genre");
    check(entries.size() == 2 && entries[0].first == "Sonic The Hedgehog 2 (World)" && entries[0].second == "Platform",
          "game info: metadat entries with the field, the others left out");
    const std::string users = "game (\n\tcomment \"Streets of Rage 2 (USA)\"\n\tusers 2\n\trom ( crc E01FA526 )\n)\n";
    check(parseMetadat(users, "users").size() == 1 && parseMetadat(users, "users")[0].second == "2",
          "game info: unquoted values");
    DetailsTable table;
    table.add("genre", genres);
    table.add("maxusers", users);
    table.add("developer", "game (\n\tcomment \"Sonic The Hedgehog 2 (World)\"\n\tdeveloper \"SEGA\"\n)\n");
    const GameDetails* d = table.find("Sonic The Hedgehog 2 (World)");
    check(d && d->genre == "Platform" && d->developer == "SEGA", "game info: the fields of one game together");
    d = table.find("Sonic The Hedgehog 2 (Europe) [!]");
    check(d && d->developer == "SEGA", "game info: another region of the same title");
    d = table.find("streets of rage 2 (usa)");
    check(d && d->players == "2", "game info: names without case");
    check(!table.find("Ecco the Dolphin (Europe)"), "game info: a game it does not know");

    const std::string answer =
        "{\"batchcomplete\":true,\"query\":{\"pages\":[{\"pageid\":2,\"ns\":0,\"title\":\"Sonic the Hedgehog\","
        "\"index\":2,\"extract\":\"Otro.\"},{\"pageid\":1,\"ns\":0,\"title\":\"Sonic the Hedgehog 2\",\"index\":1,"
        "\"extract\":\"Sonic the Hedgehog 2 es un videojuego de plataformas \\u00e9\\n\\\"Sega\\\".\"}]}}";
    const auto pages = parseWikiSearch(answer);
    check(pages.size() == 2 && pages[0].title == "Sonic the Hedgehog 2" && pages[0].index == 1,
          "game info: Wikipedia's pages in search order");
    check(!pages.empty() && pages[0].extract == "Sonic the Hedgehog 2 es un videojuego de plataformas \xc3\xa9\n\"Sega\".",
          "game info: Wikipedia's escapes");
    check(wikiTitleDistance("Sonic The Hedgehog 2", "Sonic the Hedgehog 2") == 0, "game info: the same title");
    check(wikiTitleDistance("Sonic The Hedgehog 2", "Sonic the Hedgehog") < 0, "game info: not the first game for the second");
    check(wikiTitleDistance("Golden Axe II", "Golden Axe 2 (videojuego)") == 0, "game info: roman numerals and notes");
    check(wikiTitleDistance("Phantasy Star IV", "Phantasy Star IV: The End of the Millennium") > 0,
          "game info: a longer title of the same game");
    check(wikiTitleDistance("Sonic 3D Blast", "Sonic") < 0, "game info: too little in common");
    check(wikiTitleDistance("Columns", "Ecco the Dolphin") < 0, "game info: nothing in common");
}

// The library's cover cache: QOI back to the same pixels, every kind of step in it
// (runs, the index, small and larger changes, alpha), and a file read back by its key.
void testTextureCache() {
    const int w = 61, h = 37;
    std::vector<uint32_t> px(static_cast<size_t>(w) * h);
    uint32_t seed = 12345;
    for (size_t i = 0; i < px.size(); ++i) {
        seed = seed * 1103515245u + 12345u;
        const uint32_t kind = (seed >> 16) % 6;
        const uint32_t prev = i ? px[i - 1] : 0xFF000000u;
        if (kind == 0) px[i] = prev;                                   // a run
        else if (kind == 1 && i > 20) px[i] = px[i - 17];              // probably in the index
        else if (kind == 2) px[i] = (prev & 0xFF000000u) | ((prev + 0x010101u) & 0xFFFFFFu);  // a small step
        else if (kind == 3) px[i] = (prev & 0xFF000000u) | ((prev + 0x0A0C0Eu) & 0xFFFFFFu);  // a larger one
        else if (kind == 4) px[i] = (seed & 0x00FFFFFFu) | (((seed >> 8) & 1) ? 0x80000000u : 0xFF000000u);
        else px[i] = seed;
    }
    for (size_t i = 0; i < 100; ++i) px[500 + i] = 0xFF336699u;  // longer than a run can say
    const std::vector<uint8_t> qoi = TextureCache::encodeQoi(px, w, h);
    std::vector<uint32_t> back;
    int bw = 0, bh = 0;
    check(TextureCache::decodeQoi(qoi.data(), qoi.size(), back, &bw, &bh) && bw == w && bh == h && back == px,
          "texture cache: QOI gives back the same pixels");
    check(!TextureCache::decodeQoi(qoi.data(), qoi.size() / 2, back, &bw, &bh), "texture cache: a cut QOI is refused");

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "plumsg-texture-cache-test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string source = (dir / "cover.png").u8string();
    { std::ofstream(fs::u8path(source), std::ios::binary) << "not really a png"; }
    TextureCache cache;
    cache.setFolder((dir / "cache").u8string() + "/");
    const uint64_t key = TextureCache::key(source, 7);
    check(key != 0 && key != TextureCache::key(source, 8) && TextureCache::key((dir / "none.png").u8string(), 7) == 0,
          "texture cache: the key depends on the file and how it was shrunk");
    check(!cache.load(key, back, &bw, &bh), "texture cache: nothing cached yet");
    cache.store(key, px, w, h);
    back.clear();
    check(cache.load(key, back, &bw, &bh) && back == px, "texture cache: read back from disk");
    cache.prune({key});
    check(cache.load(key, back, &bw, &bh), "texture cache: prune keeps what is used");
    cache.prune({});
    check(!cache.load(key, back, &bw, &bh), "texture cache: prune deletes what is not");
    fs::remove_all(dir, ec);
}

}  // namespace

int main() {
    testRewindBuffer();
    testFramePacer();
    testEdgeDistance();
    testLibraryNames();
    testGameInfo();
    testTextureCache();
    std::printf(failures ? "%d fallos\n" : "todo bien\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
