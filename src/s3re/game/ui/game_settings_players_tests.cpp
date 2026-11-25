// Tests of GameSettings::PlayerSetup, GameSettings::SaveSetup and
// GamePlayerSetup::GamePlayerSetup (game_settings_players.cpp).
//
// Both run on fake settings objects (a critical section at +0, the
// player-setup vector at +0x58 with a recording vtable), never on
// g_gameSettings. The vector's elements are real GamePlayerSetup objects
// (the original constructor 0x49e8f0) with random plain fields and random
// texts in their two std::strings; every case builds the same vector twice
// from one seed, one for the original and one for ours.
//
// PlayerSetup: vectors of every shape (length, capacity and filled count
// 0..4, filled -1, lengths with the sign bit, a data block without
// capacity) and indexes around and outside the range. Compared: the
// returned element (as an index into the data block), the vector's fields,
// whether the block was replaced, the elements' plain bytes and texts, and
// the vtable calls.
//
// SaveSetup: 0..20 setups; each writes into its own CMapFile (mode 2, a file
// in %TEMP%\s3re-mapfile-test). Compared: the files' chunks (keys, order,
// decoded bytes outside the stack leftovers of the original: byte +0xb of
// each record, the records past the count, chunk 0x1a +0x22..+0x23), the
// DebugLog calls, and the elements afterwards (texts and their buffers'
// share counts: the local copy must give every reference back).

#include "game/debug_log.h"
#include "game/fileformats/map_file.h"
#include "game/fileformats/map_file_test_support.h"
#include "game/fileformats/savegame_chunks.h"
#include "game/ui/game_settings_layout.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using fileformats::CMapFile;

GamePlayerSetup* __fastcall GameSettings_PlayerSetup(GameSettings* self, void* edx, std::int32_t index);
void __fastcall GameSettings_SaveSetup(GameSettings* self, void* edx, CMapFile* file, std::uint32_t tick,
                                       std::uint32_t seed);
using PlayerSetupFn = GamePlayerSetup*(__fastcall*)(GameSettings* self, void* edx, std::int32_t index);
using SaveSetupFn = void(__fastcall*)(GameSettings* self, void* edx, CMapFile* file, std::uint32_t tick,
                                      std::uint32_t seed);
extern PlayerSetupFn Original_GameSettings_PlayerSetup;
GamePlayerSetup* __fastcall GamePlayerSetup_Construct(GamePlayerSetup* self, void* edx);
using SetupConstructHookFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx);
extern SetupConstructHookFn Original_GamePlayerSetup_Construct;
GamePlayerSetup* __fastcall WrongGamePlayerSetupConstruct(GamePlayerSetup* self, void* edx);
using SetupSetFn = std::uint32_t(__fastcall*)(GamePlayerSetup* self, void* edx, std::uint32_t type,
                                             std::uint32_t byte60, std::uint32_t party, std::uint32_t race,
                                             std::uint32_t raceByte, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                             std::uint32_t colour);
std::uint32_t __fastcall GamePlayerSetup_Set(GamePlayerSetup* self, void* edx, std::uint32_t type, std::uint32_t byte60,
                                             std::uint32_t party, std::uint32_t race, std::uint32_t raceByte,
                                             std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t colour);
extern SetupSetFn Original_GamePlayerSetup_Set;
GamePlayerSetup* __fastcall GamePlayerSetup_ConstructId(GamePlayerSetup* self, void* edx, std::uint32_t id);
using SetupConstructIdFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx, std::uint32_t id);
extern SetupConstructIdFn Original_GamePlayerSetup_ConstructId;
extern SaveSetupFn Original_GameSettings_SaveSetup;
void __fastcall GamePlayerSetup_Destruct(void* self, void* edx);

namespace {

using SetupConstructFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx);
const auto kSetupConstruct = reinterpret_cast<SetupConstructFn>(0x0049e8f0);

// ---------------------------------------------------------------------------
// Fake settings
// ---------------------------------------------------------------------------

struct Recorder {
    std::vector<std::uint32_t> calls;  // (vector offset in its settings object, argument) per call
    const std::uint8_t* base = nullptr;
};
Recorder* g_recorder = nullptr;

void __fastcall RecordNotify(PlayerSetupVector* self, void*, std::uint32_t argument) {
    g_recorder->calls.push_back(
        static_cast<std::uint32_t>(reinterpret_cast<const std::uint8_t*>(self) - g_recorder->base));
    g_recorder->calls.push_back(argument);
}

void __fastcall UndeclaredSlot(PlayerSetupVector*, void*) {
    g_recorder->calls.push_back(0xbad);
}

const void* const kFakeVtable[4] = {reinterpret_cast<const void*>(&UndeclaredSlot),
                                    reinterpret_cast<const void*>(&RecordNotify),
                                    reinterpret_cast<const void*>(&UndeclaredSlot),
                                    reinterpret_cast<const void*>(&UndeclaredSlot)};

// A settings object with an initialized critical section.
class FakeSettings {
public:
    explicit FakeSettings(test::Rng& rng) {
        rng.Fill(bytes_, sizeof(bytes_));
        InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(bytes_));
        PlayerSetupVector& v = get()->player_setups;
        v.vtable = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(kFakeVtable));
        v.data = nullptr;
        v.capacity = 0;
        v.filled = 0;
        v.length = 0;
    }
    ~FakeSettings() {
        PlayerSetupVector& v = get()->player_setups;
        for (std::int32_t i = 0; i < v.filled; ++i) {
            GamePlayerSetup_Destruct(&v.data[i], nullptr);
        }
        msvc6::GameOperatorDelete(v.data);
        DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(bytes_));
    }
    FakeSettings(const FakeSettings&) = delete;
    FakeSettings& operator=(const FakeSettings&) = delete;

    GameSettings* get() { return reinterpret_cast<GameSettings*>(bytes_); }
    const std::uint8_t* base() const { return bytes_; }

private:
    alignas(8) std::uint8_t bytes_[0x100];
};

// The plain parts of a GamePlayerSetup (its strings, 16-bit string and
// vector excluded).
constexpr std::size_t kPlainRanges[][2] = {{0x00, 0x18}, {0x54, 0xbc}};

std::string TextOf(const msvc6::String& s) {
    return s.ptr != nullptr ? std::string(s.ptr, s.len) : std::string();
}

std::string RandomText(test::Rng& rng) {
    std::string text(rng.Below(3) == 0 ? 0 : 1 + rng.Below(40), 'a');
    for (char& c : text) {
        c = static_cast<char>('a' + rng.Below(26));
    }
    return text;
}

void FillSetup(test::Rng& rng, GamePlayerSetup* setup) {
    kSetupConstruct(setup, nullptr);
    auto* bytes = reinterpret_cast<std::uint8_t*>(setup);
    for (const auto& range : kPlainRanges) {
        rng.Fill(bytes + range[0], range[1] - range[0]);
    }
    if (rng.Chance(1, 4)) {  // small values, like the game's
        setup->party = rng.Below(20);
        setup->type = rng.Below(3);
        setup->race = rng.Chance(1, 4) ? 0xff : rng.Below(4);
    }
    const std::string a = RandomText(rng);
    const std::string b = RandomText(rng);
    msvc6::String_AssignChars(&setup->text18, nullptr, a.c_str(), static_cast<std::uint32_t>(a.size()));
    msvc6::String_AssignChars(&setup->textBC, nullptr, b.c_str(), static_cast<std::uint32_t>(b.size()));
}

// A vector of `filled` elements in a block of `slots` elements (none when
// both are 0 and `block` is false).
void BuildVector(test::Rng& rng, GameSettings* settings, std::uint32_t length, std::uint32_t capacity,
                 std::int32_t filled, bool block) {
    PlayerSetupVector& v = settings->player_setups;
    const std::uint32_t built = filled > 0 ? static_cast<std::uint32_t>(filled) : 0;
    std::uint32_t slots = capacity > built ? capacity : built;
    if (slots == 0 && block) {
        slots = 1;
    }
    v.data = slots != 0 ? static_cast<GamePlayerSetup*>(msvc6::GameOperatorNew(slots * sizeof(GamePlayerSetup)))
                        : nullptr;
    for (std::uint32_t i = 0; i < built; ++i) {
        FillSetup(rng, &v.data[i]);
    }
    v.capacity = capacity;
    v.filled = filled;
    v.length = length;
}

// What a vector's elements hold: plain bytes and texts.
std::vector<std::uint8_t> Contents(const PlayerSetupVector& v) {
    std::vector<std::uint8_t> out;
    for (std::int32_t i = 0; i < v.filled; ++i) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&v.data[i]);
        for (const auto& range : kPlainRanges) {
            out.insert(out.end(), bytes + range[0], bytes + range[1]);
        }
        for (const msvc6::String* s : {&v.data[i].text18, &v.data[i].textBC}) {
            const std::string text = TextOf(*s);
            out.insert(out.end(), text.begin(), text.end());
            out.push_back(0xee);
            out.push_back(s->ptr != nullptr ? static_cast<std::uint8_t>(s->ptr[-1]) : 0xdd);  // share count
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// PlayerSetup
// ---------------------------------------------------------------------------

struct AccessOutcome {
    std::int64_t element;  // the returned pointer as an element index into the final block
    std::uint32_t vtable, unk04, capacity, length;
    std::int32_t filled;
    bool blockReplaced, blockNull;
    std::vector<std::uint8_t> contents;
    std::vector<std::uint32_t> calls;

    bool operator==(const AccessOutcome& o) const {
        return element == o.element && vtable == o.vtable && unk04 == o.unk04 && capacity == o.capacity &&
               length == o.length && filled == o.filled && blockReplaced == o.blockReplaced &&
               blockNull == o.blockNull && contents == o.contents && calls == o.calls;
    }
};

struct AccessCase {
    std::uint64_t seed;
    std::uint32_t length, capacity;
    std::int32_t filled;
    bool block;
    std::int32_t index;
};

AccessCase DrawAccessCase(test::Rng& rng) {
    AccessCase c{};
    c.seed = rng.Next64();
    c.capacity = rng.Below(5);
    c.filled = rng.Chance(1, 10) ? -1 : static_cast<std::int32_t>(rng.Below(c.capacity + 1));
    if (c.capacity == 0 && rng.Chance(1, 3)) {
        c.filled = 1;  // elements without capacity: copied by the growth
    }
    c.length = rng.Chance(1, 3) ? 0 : rng.Below(5);
    if (rng.Chance(1, 20)) {
        c.length = 0x80000000u + rng.Below(3);  // the signed range check
    }
    c.block = rng.Chance(1, 2);
    c.index = rng.Range(-3, 7);
    if (rng.Chance(1, 20)) {
        c.index = static_cast<std::int32_t>(rng.Next32());
    }
    return c;
}

AccessOutcome RunAccess(PlayerSetupFn fn, const AccessCase& c) {
    test::Rng rng(c.seed);
    FakeSettings settings(rng);
    BuildVector(rng, settings.get(), c.length, c.capacity, c.filled, c.block);
    Recorder recorder;
    recorder.base = settings.base();
    g_recorder = &recorder;
    PlayerSetupVector& v = settings.get()->player_setups;
    const GamePlayerSetup* before = v.data;
    const GamePlayerSetup* result = fn(settings.get(), nullptr, c.index);
    g_recorder = nullptr;
    AccessOutcome out;
    out.element = (reinterpret_cast<std::intptr_t>(result) - reinterpret_cast<std::intptr_t>(v.data)) /
                  static_cast<std::intptr_t>(sizeof(GamePlayerSetup));
    out.vtable = v.vtable;
    out.unk04 = v.unk04;
    out.capacity = v.capacity;
    out.length = v.length;
    out.filled = v.filled;
    out.blockReplaced = v.data != before;
    out.blockNull = v.data == nullptr;
    out.contents = Contents(v);
    out.calls = std::move(recorder.calls);
    return out;
}

void CompareAccess(test::Context& ctx, PlayerSetupFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        const AccessCase c = DrawAccessCase(ctx.rng());
        const AccessOutcome a = RunAccess(Original_GameSettings_PlayerSetup, c);
        const AccessOutcome b = RunAccess(candidate, c);
        if (!S3RE_EXPECT(a == b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: length 0x%x capacity %u filled %d index %d: element %lld/%lld",
                     i, c.length, c.capacity, c.filled, c.index, static_cast<long long>(a.element),
                     static_cast<long long>(b.element));
            return;
        }
    }
}

// Wrong: an index past the end gives the first element.
GamePlayerSetup* __fastcall WrongPlayerSetup(GameSettings* self, void* edx, std::int32_t index) {
    if (index >= static_cast<std::int32_t>(self->player_setups.length) && self->player_setups.length != 0) {
        index = 0;
    }
    return GameSettings_PlayerSetup(self, edx, index);
}

// ---------------------------------------------------------------------------
// SaveSetup
// ---------------------------------------------------------------------------

constexpr std::uint32_t kUnwrittenRecordByte = offsetof(fileformats::SavedPlayerSetup, unwritten_b);

std::vector<bool> UnwrittenBytes(std::uint32_t key, std::size_t size, std::uint32_t count) {
    std::vector<bool> mask(size, false);
    auto maskRange = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to && i < size; ++i) {
            mask[i] = true;
        }
    };
    if (key == static_cast<std::uint32_t>(fileformats::SaveChunk::SetupPlayers)) {
        for (std::uint32_t r = 0; r < fileformats::kSavedPlayerSetups; ++r) {
            const std::size_t record = offsetof(fileformats::SaveSetupPlayersChunk, players) +
                                       r * sizeof(fileformats::SavedPlayerSetup);
            if (r < count) {
                maskRange(record + kUnwrittenRecordByte, record + kUnwrittenRecordByte + 1);
            } else {
                maskRange(record, record + sizeof(fileformats::SavedPlayerSetup));
            }
        }
    } else if (key == static_cast<std::uint32_t>(fileformats::SaveChunk::Setup1A)) {
        maskRange(offsetof(fileformats::SaveSetup1AChunk, unwritten_22), sizeof(fileformats::SaveSetup1AChunk));
    }
    return mask;
}

struct SaveOutcome {
    std::vector<std::uint32_t> keys;
    std::vector<std::vector<std::uint8_t>> chunks;  // decoded, unwritten bytes cleared
    std::vector<DebugLogCall> log;
    std::vector<std::uint8_t> contents;
    std::vector<std::uint32_t> calls;
    std::vector<std::uint8_t> settingsAfter;

    bool operator==(const SaveOutcome& o) const {
        return keys == o.keys && chunks == o.chunks && log == o.log && contents == o.contents && calls == o.calls &&
               settingsAfter == o.settingsAfter;
    }
};

struct SaveCase {
    std::uint64_t seed;
    std::uint32_t count;
    std::uint32_t tick, seedWord;
};

SaveOutcome RunSave(SaveSetupFn fn, const SaveCase& c, const std::string& path) {
    test::Rng rng(c.seed);
    FakeSettings settings(rng);
    BuildVector(rng, settings.get(), c.count, c.count + rng.Below(3), static_cast<std::int32_t>(c.count), false);
    Recorder recorder;
    recorder.base = settings.base();
    g_recorder = &recorder;
    DeleteFileA(path.c_str());
    CMapFile* file = mapfile_test::OpenMapFile(path, fileformats::kOpenWrite);
    SaveOutcome out;
    {
        DebugLogRecorder log;
        fn(settings.get(), nullptr, file, c.tick, c.seedWord);
        out.log = log.Take();
    }
    mapfile_test::DeleteMapFile(file);
    g_recorder = nullptr;
    const std::vector<std::uint8_t> bytes = mapfile_test::ReadWholeFile(path);
    DeleteFileA(path.c_str());
    for (mapfile_test::ParsedChunk& chunk : mapfile_test::ParseContainer(bytes)) {
        const std::vector<bool> mask = UnwrittenBytes(chunk.key, chunk.bytes.size(), c.count);
        for (std::size_t i = 0; i < chunk.bytes.size(); ++i) {
            if (mask[i]) {
                chunk.bytes[i] = 0;
            }
        }
        out.keys.push_back(chunk.key);
        out.chunks.push_back(std::move(chunk.bytes));
    }
    out.contents = Contents(settings.get()->player_setups);
    out.calls = std::move(recorder.calls);
    const auto* plain = settings.base() + 0x18;  // past the critical section
    out.settingsAfter.assign(plain, plain + 0x100 - 0x18);
    const std::size_t dataField = offsetof(GameSettings, player_setups) + offsetof(PlayerSetupVector, data) - 0x18;
    std::memset(out.settingsAfter.data() + dataField, 0, sizeof(void*));  // a heap address
    return out;
}

void CompareSave(test::Context& ctx, SaveSetupFn candidate, int cases) {
    const std::string folder = mapfile_test::TempFolder();
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        SaveCase c{};
        c.seed = rng.Next64();
        c.count = rng.Chance(1, 10) ? 0 : 1 + rng.Below(fileformats::kSavedPlayerSetups);
        c.tick = rng.Next32();
        c.seedWord = rng.Next32();
        const SaveOutcome a = RunSave(Original_GameSettings_SaveSetup, c, folder + "\\setup-original.sav");
        const SaveOutcome b = RunSave(candidate, c, folder + "\\setup-ours.sav");
        if (!S3RE_EXPECT(a.keys.size() == 4 && a == b)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: %u setups; %zu/%zu chunks", i, c.count, a.keys.size(),
                     b.keys.size());
            return;
        }
    }
}

// Wrong: saves the type in place of the party.
void __fastcall WrongSaveSetup(GameSettings* self, void* edx, CMapFile* file, std::uint32_t tick,
                               std::uint32_t seed) {
    PlayerSetupVector& v = self->player_setups;
    std::vector<std::uint32_t> parties;
    for (std::int32_t i = 0; i < v.filled; ++i) {
        parties.push_back(v.data[i].party);
        v.data[i].party = v.data[i].type;
    }
    GameSettings_SaveSetup(self, edx, file, tick, seed);
    for (std::int32_t i = 0; i < v.filled; ++i) {
        v.data[i].party = parties[static_cast<std::size_t>(i)];
    }
}

// ---------------------------------------------------------------------------
// GamePlayerSetup::GamePlayerSetup
// ---------------------------------------------------------------------------
// Two random records constructed by the original and ours: every byte must
// match except the three allocator bytes (the original's uninitialized
// local) and the vector's data pointer (a heap block each; both non-null).

constexpr std::size_t kSetupAllocatorBytes[] = {0x18, 0x28, 0xbc};
constexpr std::size_t kSetupVectorData = 0x38 + 8;

void CompareSetupConstruct(test::Context& ctx, SetupConstructHookFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        alignas(4) std::uint8_t a[sizeof(GamePlayerSetup)], b[sizeof(GamePlayerSetup)];
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        auto* sa = reinterpret_cast<GamePlayerSetup*>(a);
        auto* sb = reinterpret_cast<GamePlayerSetup*>(b);
        GamePlayerSetup* ra = Original_GamePlayerSetup_Construct(sa, nullptr);
        GamePlayerSetup* rb = candidate(sb, nullptr);
        // Compare copies with the known differences cleared.
        std::uint8_t ca[sizeof(GamePlayerSetup)], cb[sizeof(GamePlayerSetup)];
        std::memcpy(ca, a, sizeof(ca));
        std::memcpy(cb, b, sizeof(cb));
        void* dataA = nullptr;
        void* dataB = nullptr;
        std::memcpy(&dataA, ca + kSetupVectorData, sizeof(void*));
        std::memcpy(&dataB, cb + kSetupVectorData, sizeof(void*));
        for (const std::size_t at : kSetupAllocatorBytes) {
            ca[at] = cb[at] = 0;
        }
        std::memset(ca + kSetupVectorData, 0, sizeof(void*));
        std::memset(cb + kSetupVectorData, 0, sizeof(void*));
        const bool same = S3RE_EXPECT(ra == sa && rb == sb && dataA != nullptr && dataB != nullptr) &&
                          S3RE_EXPECT_MEM_EQ(ca, cb, sizeof(ca));
        GamePlayerSetup_Destruct(sa, nullptr);
        GamePlayerSetup_Destruct(sb, nullptr);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// The id constructor: every byte but the vector's data pointer (the
// allocator bytes are the id's low byte in both).
void CompareSetupConstructId(test::Context& ctx, SetupConstructIdFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        alignas(4) std::uint8_t a[sizeof(GamePlayerSetup)], b[sizeof(GamePlayerSetup)];
        ctx.rng().Fill(a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        const std::uint32_t id = ctx.rng().Chance(1, 3) ? ctx.rng().Below(20) : ctx.rng().Next32();
        auto* sa = reinterpret_cast<GamePlayerSetup*>(a);
        auto* sb = reinterpret_cast<GamePlayerSetup*>(b);
        GamePlayerSetup* ra = Original_GamePlayerSetup_ConstructId(sa, nullptr, id);
        GamePlayerSetup* rb = candidate(sb, nullptr, id);
        std::uint8_t ca[sizeof(GamePlayerSetup)], cb[sizeof(GamePlayerSetup)];
        std::memcpy(ca, a, sizeof(ca));
        std::memcpy(cb, b, sizeof(cb));
        std::memset(ca + kSetupVectorData, 0, sizeof(void*));
        std::memset(cb + kSetupVectorData, 0, sizeof(void*));
        const bool same = S3RE_EXPECT(ra == sa && rb == sb) && S3RE_EXPECT_MEM_EQ(ca, cb, sizeof(ca));
        GamePlayerSetup_Destruct(sa, nullptr);
        GamePlayerSetup_Destruct(sb, nullptr);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: id 0x%x", i, id);
            return;
        }
    }
}

// Set on random records (+0x6c / +0x70 often -1) with random arguments:
// every byte and EAX.
void CompareSetupSet(test::Context& ctx, SetupSetFn candidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        alignas(4) std::uint8_t a[sizeof(GamePlayerSetup)], b[sizeof(GamePlayerSetup)];
        ctx.rng().Fill(a, sizeof(a));
        for (const std::size_t at : {std::size_t{0x6c}, std::size_t{0x70}}) {
            if (ctx.rng().Chance(1, 2)) {
                std::memset(a + at, 0xff, 4);
            }
        }
        std::memcpy(b, a, sizeof(a));
        std::uint32_t args[9];
        ctx.rng().Fill(args, sizeof(args));
        const std::uint32_t ra = Original_GamePlayerSetup_Set(reinterpret_cast<GamePlayerSetup*>(a), nullptr, args[0],
                                                              args[1], args[2], args[3], args[4], args[5], args[6],
                                                              args[7], args[8]);
        const std::uint32_t rb = candidate(reinterpret_cast<GamePlayerSetup*>(b), nullptr, args[0], args[1], args[2],
                                           args[3], args[4], args[5], args[6], args[7], args[8]);
        if (!S3RE_EXPECT_EQ(ra, rb) || !S3RE_EXPECT_MEM_EQ(a, b, sizeof(a))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            return;
        }
    }
}

// Wrong: the initial party always set.
std::uint32_t __fastcall WrongSet(GamePlayerSetup* self, void* edx, std::uint32_t type, std::uint32_t byte60,
                                  std::uint32_t party, std::uint32_t race, std::uint32_t raceByte, std::uint32_t a,
                                  std::uint32_t b, std::uint32_t c, std::uint32_t colour) {
    const std::uint32_t result = GamePlayerSetup_Set(self, edx, type, byte60, party, race, raceByte, a, b, c, colour);
    std::memcpy(reinterpret_cast<std::uint8_t*>(self) + 0x70, &party, 4);
    return result;
}

// Wrong: the allocator bytes 0, as in the default constructor.
GamePlayerSetup* __fastcall WrongConstructId(GamePlayerSetup* self, void* edx, std::uint32_t id) {
    GamePlayerSetup_ConstructId(self, edx, id);
    self->text18.allocator = 0;
    return self;
}

}  // namespace

S3RE_TEST(GamePlayerSetup_Set_MatchesOriginal) {
    CompareSetupSet(ctx, &GamePlayerSetup_Set, 3000);
}
S3RE_TEST(GamePlayerSetup_Set_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetupSet(probe, &WrongSet, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GamePlayerSetup_ConstructId_MatchesOriginal) {
    CompareSetupConstructId(ctx, &GamePlayerSetup_ConstructId, 2000);
}
S3RE_TEST(GamePlayerSetup_ConstructId_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetupConstructId(probe, &WrongConstructId, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GamePlayerSetup_Construct_MatchesOriginal) {
    CompareSetupConstruct(ctx, &GamePlayerSetup_Construct, 2000);
}
S3RE_TEST(GamePlayerSetup_Construct_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetupConstruct(probe, &WrongGamePlayerSetupConstruct, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GameSettings_PlayerSetup_MatchesOriginal) {
    CompareAccess(ctx, &GameSettings_PlayerSetup, 3000);
}
S3RE_TEST(GameSettings_PlayerSetup_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAccess(probe, &WrongPlayerSetup, 300);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(GameSettings_SaveSetup_MatchesOriginal) {
    CompareSave(ctx, &GameSettings_SaveSetup, 300);
}
S3RE_TEST(GameSettings_SaveSetup_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSave(probe, &WrongSaveSetup, 30);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
