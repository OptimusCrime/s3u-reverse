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
#include "game/vtables.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "msvc6/wstring.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <algorithm>
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
GamePlayerSetup* __fastcall GameSettings_PlayerSetupById(GameSettings* self, void* edx, std::uint32_t id);
using PlayerSetupByIdFn = GamePlayerSetup*(__fastcall*)(GameSettings* self, void* edx, std::uint32_t id);
extern PlayerSetupByIdFn Original_GameSettings_PlayerSetupById;
std::uint32_t __fastcall GameSettings_HasPlayerId(GameSettings* self, void* edx, std::uint32_t id);
using HasPlayerIdFn = std::uint32_t(__fastcall*)(GameSettings* self, void* edx, std::uint32_t id);
extern HasPlayerIdFn Original_GameSettings_HasPlayerId;
GamePlayerSetup* __fastcall GamePlayerSetup_Construct(GamePlayerSetup* self, void* edx);
using SetupConstructHookFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx);
extern SetupConstructHookFn Original_GamePlayerSetup_Construct;
GamePlayerSetup* __fastcall WrongGamePlayerSetupConstruct(GamePlayerSetup* self, void* edx);
GamePlayerSetup* __fastcall GamePlayerSetup_ConstructId(GamePlayerSetup* self, void* edx, std::uint32_t id);
using SetupConstructIdFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx, std::uint32_t id);
extern SetupConstructIdFn Original_GamePlayerSetup_ConstructId;
extern SaveSetupFn Original_GameSettings_SaveSetup;
void __fastcall GamePlayerSetup_Destruct(void* self, void* edx);
using SetupSetFn = std::uint32_t(__fastcall*)(GamePlayerSetup* self, void* edx, std::uint32_t type,
                                             std::uint32_t byte60, std::uint32_t party, std::uint32_t race,
                                             std::uint32_t raceByte, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                             std::uint32_t colour);
std::uint32_t __fastcall GamePlayerSetup_Set(GamePlayerSetup* self, void* edx, std::uint32_t type, std::uint32_t byte60,
                                             std::uint32_t party, std::uint32_t race, std::uint32_t raceByte,
                                             std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t colour);
extern SetupSetFn Original_GamePlayerSetup_Set;
using SetupCopyHookFn = GamePlayerSetup*(__fastcall*)(GamePlayerSetup* self, void* edx, const GamePlayerSetup* from);
GamePlayerSetup* __fastcall GamePlayerSetup_CopyConstruct(GamePlayerSetup* self, void* edx,
                                                          const GamePlayerSetup* from);
GamePlayerSetup* __fastcall GamePlayerSetup_Assign(GamePlayerSetup* self, void* edx, const GamePlayerSetup* from);
extern SetupCopyHookFn Original_GamePlayerSetup_CopyConstruct;
extern SetupCopyHookFn Original_GamePlayerSetup_Assign;

// game_settings_vectors.cpp
using VecResizeFn = std::uint32_t(__fastcall*)(watcom::VectorBase* self, void* edx, std::uint32_t n);
using VecInsertAtFn = std::uint32_t(__fastcall*)(watcom::VectorBase* self, void* edx, std::int32_t index,
                                                 const void* value);
using VecCopyContentsFn = void(__fastcall*)(watcom::VectorBase* self, void* edx, const watcom::VectorBase* from);
using TeamCopyHookFn = CTeam*(__fastcall*)(CTeam* self, void* edx, const CTeam* from);
using AddPlayerIdHookFn = std::uint32_t(__fastcall*)(CTeam* self, void* edx, std::int32_t id);
using LogSlotHookFn = std::uint32_t(__fastcall*)(const GamePlayerSetup* self, void* edx);
std::uint32_t __fastcall PlayerVector_Resize(watcom::VectorBase* self, void* edx, std::uint32_t n);
std::uint32_t __fastcall TeamVector_Resize(watcom::VectorBase* self, void* edx, std::uint32_t n);
std::uint32_t __fastcall PlayerVector_InsertAt(watcom::VectorBase* self, void* edx, std::int32_t index,
                                               const void* value);
std::uint32_t __fastcall TeamVector_InsertAt(watcom::VectorBase* self, void* edx, std::int32_t index, const void* value);
void __fastcall PlayerBareVector_CopyContents(watcom::VectorBase* self, void* edx, const watcom::VectorBase* from);
CTeam* __fastcall CTeam_CopyConstruct(CTeam* self, void* edx, const CTeam* from);
CTeam* __fastcall CTeam_Assign(CTeam* self, void* edx, const CTeam* from);
std::uint32_t __fastcall CTeam_AddPlayerId(CTeam* self, void* edx, std::int32_t id);
std::uint32_t __fastcall GamePlayerSetup_LogSlot(const GamePlayerSetup* self, void* edx);
extern VecResizeFn Original_PlayerVector_Resize;
extern VecResizeFn Original_TeamVector_Resize;
extern VecInsertAtFn Original_PlayerVector_InsertAt;
extern VecInsertAtFn Original_TeamVector_InsertAt;
extern VecCopyContentsFn Original_PlayerBareVector_CopyContents;
extern TeamCopyHookFn Original_CTeam_CopyConstruct;
extern TeamCopyHookFn Original_CTeam_Assign;
extern AddPlayerIdHookFn Original_CTeam_AddPlayerId;
extern LogSlotHookFn Original_GamePlayerSetup_LogSlot;

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

// PlayerSetupById: the vector's find (slot 3) is a fake that records the
// key's id (+0x10) and the occurrence and finds the id at index id % 9 - 2
// unless id % 4 == 0; the access then runs as in PlayerSetup's cases.
std::uint32_t __fastcall FakeFind(PlayerSetupVector*, void*, const GamePlayerSetup* key, std::int32_t occurrence,
                                  std::int32_t* index) {
    g_recorder->calls.push_back(0xf1d);
    g_recorder->calls.push_back(key->value10);
    g_recorder->calls.push_back(static_cast<std::uint32_t>(occurrence));
    g_recorder->calls.push_back(static_cast<std::uint32_t>(*index));
    if (key->value10 % 4 == 0) {
        return 0;
    }
    *index = static_cast<std::int32_t>(key->value10 % 9) - 2;
    return 1;
}

const void* const kFakeFindVtable[4] = {reinterpret_cast<const void*>(&UndeclaredSlot),
                                        reinterpret_cast<const void*>(&RecordNotify),
                                        reinterpret_cast<const void*>(&UndeclaredSlot),
                                        reinterpret_cast<const void*>(&FakeFind)};

PlayerSetupByIdFn g_byIdCandidate = nullptr;
std::uint32_t g_byIdId = 0;

HasPlayerIdFn g_hasIdCandidate = nullptr;
std::uint32_t g_hasIdResult = 0;

GamePlayerSetup* __fastcall RunById(GameSettings* self, void* edx, std::int32_t) {
    self->player_setups.vtable = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(kFakeFindVtable));
    g_hasIdResult = g_hasIdCandidate(self, edx, g_byIdId) & 0xff;
    return g_byIdCandidate(self, edx, g_byIdId);
}

void CompareAccessById(test::Context& ctx, PlayerSetupByIdFn candidate, HasPlayerIdFn hasCandidate, int cases) {
    for (int i = 0; i < cases; ++i) {
        const AccessCase c = DrawAccessCase(ctx.rng());
        g_byIdId = ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(20);
        g_byIdCandidate = Original_GameSettings_PlayerSetupById;
        g_hasIdCandidate = Original_GameSettings_HasPlayerId;
        const AccessOutcome a = RunAccess(&RunById, c);
        const std::uint32_t hasA = g_hasIdResult;
        g_byIdCandidate = candidate;
        g_hasIdCandidate = hasCandidate;
        const AccessOutcome b = RunAccess(&RunById, c);
        if (!S3RE_EXPECT(a == b) || !S3RE_EXPECT_EQ(hasA, g_hasIdResult)) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: length 0x%x capacity %u filled %d id %u: element %lld/%lld", i,
                     c.length, c.capacity, c.filled, g_byIdId, static_cast<long long>(a.element),
                     static_cast<long long>(b.element));
            return;
        }
    }
}

// Wrong: looks up the next id for ids that are 1 modulo 4.
GamePlayerSetup* __fastcall WrongPlayerSetupById(GameSettings* self, void* edx, std::uint32_t id) {
    if (id % 4 == 1) {
        id += 1;
    }
    return GameSettings_PlayerSetupById(self, edx, id);
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

// Wrong: the allocator bytes 0, as in the default constructor.
GamePlayerSetup* __fastcall WrongConstructId(GamePlayerSetup* self, void* edx, std::uint32_t id) {
    GamePlayerSetup_ConstructId(self, edx, id);
    self->text18.allocator = 0;
    return self;
}

// ---------------------------------------------------------------------------
// Set, copy constructor, operator=
// ---------------------------------------------------------------------------

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

constexpr std::size_t kWideName = 0x28;
// The pointer fields: the strings' text pointers and the vector's data.
constexpr std::size_t kTextPointers[] = {0x18 + 4, kWideName + 4, 0xbc + 4};

msvc6::WString* WideOf(GamePlayerSetup* setup) {
    return reinterpret_cast<msvc6::WString*>(reinterpret_cast<std::uint8_t*>(setup) + kWideName);
}

// A setup as FillSetup makes it, with a 16-bit text and odd share counts
// (0xfe: copied instead of shared; 0xff frozen; 1: shared with nobody).
void FillSetupForCopy(test::Rng& rng, GamePlayerSetup* setup) {
    FillSetup(rng, setup);
    std::vector<std::uint16_t> words(2 + rng.Below(30), 0);
    words[0] = 0xff00;  // a frozen source: copied, never shared
    for (std::size_t k = 1; k + 1 < words.size(); ++k) {
        words[k] = static_cast<std::uint16_t>(1 + rng.Below(0xfffe));
    }
    msvc6::WString source{};
    source.ptr = &words[1];
    source.len = static_cast<std::uint32_t>(words.size() - 2);
    source.res = source.len;
    msvc6::WString_AssignSubstr(WideOf(setup), nullptr, &source, 0, 0xFFFFFFFFu);
    for (const std::size_t at : kTextPointers) {
        std::uint8_t* text = nullptr;
        std::memcpy(&text, reinterpret_cast<std::uint8_t*>(setup) + at, sizeof(text));
        if (text != nullptr && rng.Chance(1, 3)) {
            static constexpr std::uint8_t kCounts[] = {1, 0xfd, 0xfe, 0xff};
            text[-1] = kCounts[rng.Below(4)];
        }
    }
}

// A setup's bytes with its pointers replaced by what they point to
// relative to `from` (1 = from's buffer, 2 = null, 3 = its own old buffer,
// 4 = another block), then its texts and share counts.
std::vector<std::uint8_t> CopySnapshot(const GamePlayerSetup* setup, const GamePlayerSetup* from,
                                       const std::uintptr_t (&oldPointers)[3]) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(setup);
    std::vector<std::uint8_t> out(bytes, bytes + sizeof(GamePlayerSetup));
    for (int k = 0; k < 3; ++k) {
        const std::size_t at = kTextPointers[k];
        std::uintptr_t ptr = 0, fromPtr = 0;
        std::memcpy(&ptr, bytes + at, sizeof(ptr));
        std::memcpy(&fromPtr, reinterpret_cast<const std::uint8_t*>(from) + at, sizeof(fromPtr));
        const std::uint32_t kind = ptr == 0 ? 2 : ptr == fromPtr ? 1 : ptr == oldPointers[k] ? 3 : 4;
        std::memcpy(out.data() + at, &kind, 4);
        std::uint32_t len = 0;
        std::memcpy(&len, bytes + at + 4, 4);
        if (ptr != 0) {
            const auto* text = reinterpret_cast<const std::uint8_t*>(ptr);
            const std::size_t size = (k == 1 ? 2 : 1) * (len + 1);
            out.insert(out.end(), text - 1, text + size);
        }
    }
    const std::uint32_t dataKind = std::uint32_t{0x00da7a00} | (*reinterpret_cast<void* const*>(bytes + 0x40) != nullptr);
    std::memcpy(out.data() + 0x40, &dataKind, 4);
    return out;
}

void PointersOf(const GamePlayerSetup* setup, std::uintptr_t (&pointers)[3]) {
    for (int k = 0; k < 3; ++k) {
        std::memcpy(&pointers[k], reinterpret_cast<const std::uint8_t*>(setup) + kTextPointers[k], sizeof(pointers[k]));
    }
}

// The copy constructor on random target bytes, operator= on a second filled
// setup (and on itself). Both sides build their records from one seed.
// Compared: the target and the source (snapshots), EAX.
void CompareSetupCopy(test::Context& ctx, SetupCopyHookFn candidate, bool assign, int cases) {
    for (int i = 0; i < cases; ++i) {
        const std::uint64_t seed = ctx.rng().Next64();
        const bool self = assign && ctx.rng().Chance(1, 8);
        std::vector<std::uint8_t> snapshots[2][2];
        std::uint32_t results[2] = {};
        for (int side = 0; side < 2; ++side) {
            test::Rng rng(seed);
            alignas(4) std::uint8_t fromBytes[sizeof(GamePlayerSetup)], toBytes[sizeof(GamePlayerSetup)];
            auto* from = reinterpret_cast<GamePlayerSetup*>(fromBytes);
            auto* to = reinterpret_cast<GamePlayerSetup*>(toBytes);
            FillSetupForCopy(rng, from);
            if (assign) {
                FillSetupForCopy(rng, to);
            } else {
                rng.Fill(toBytes, sizeof(toBytes));
            }
            const GamePlayerSetup* source = self ? to : from;
            std::uintptr_t oldPointers[3];
            PointersOf(to, oldPointers);
            const SetupCopyHookFn fn = side == 0 ? (assign ? Original_GamePlayerSetup_Assign
                                                           : Original_GamePlayerSetup_CopyConstruct)
                                                 : candidate;
            GamePlayerSetup* result = fn(to, nullptr, source);
            results[side] = result == to;
            snapshots[side][0] = CopySnapshot(to, source, oldPointers);
            std::uintptr_t fromPointers[3];
            PointersOf(from, fromPointers);
            snapshots[side][1] = CopySnapshot(from, from, fromPointers);
            if (!self) {
                GamePlayerSetup_Destruct(to, nullptr);
            }
            GamePlayerSetup_Destruct(self ? to : from, nullptr);
            if (self) {
                GamePlayerSetup_Destruct(from, nullptr);
            }
        }
        // The copy constructor's allocator bytes are the low byte of `from`'s
        // address: the same on both sides (the same stack slot).
        if (!S3RE_EXPECT_EQ(results[0], results[1]) ||
            !S3RE_EXPECT(snapshots[0][0] == snapshots[1][0]) || !S3RE_EXPECT(snapshots[0][1] == snapshots[1][1])) {
            ctx.Fail(__FILE__, __LINE__, "  case %d (self %d)", i, self);
            return;
        }
    }
}

// Wrong: operator= that never shares (always copies the narrow strings).
GamePlayerSetup* __fastcall WrongAssign(GamePlayerSetup* self, void* edx, const GamePlayerSetup* from) {
    GamePlayerSetup_Assign(self, edx, from);
    if (self != from && self->text18.ptr != nullptr && self->text18.ptr == from->text18.ptr) {
        msvc6::String_Copy(&self->text18, nullptr, self->text18.len);
    }
    return self;
}

// Wrong: the copy constructor's allocator byte of +0xbc from the source
// string instead of the argument's low byte (differ in their lowest bit).
GamePlayerSetup* __fastcall WrongCopyConstruct(GamePlayerSetup* self, void* edx, const GamePlayerSetup* from) {
    GamePlayerSetup_CopyConstruct(self, edx, from);
    self->textBC.allocator ^= 1;
    return self;
}

// ---------------------------------------------------------------------------
// The vectors of player setups and teams (game_settings_vectors.cpp)
// ---------------------------------------------------------------------------
//
// Real records on the game's heap, every case built twice from one seed (one
// side for the original, one for ours). Compared: EAX and snapshots of the
// vectors (fields with the data pointer as null / not null, then every
// filled element's contents: plain bytes, texts and share counts, a team's
// ids and players), the values passed in, and the DebugLog calls.

using watcom::VectorBase;

std::uintptr_t Address(std::uint32_t value) {
    return static_cast<std::uintptr_t>(value);
}

// Left out: the allocator bytes of the std::strings (+0x18, +0xbc), which
// the copy constructor takes from the low byte of the source's address
// (differs between the two sides' heap blocks; CompareSetupCopy checks them
// with sources at the same address), and the padding bytes no copy function
// writes (heap leftovers of each side's new block).
constexpr std::size_t kUnwrittenPadding[][2] = {{0x0d, 0x10}, {0x19, 0x1c}, {0x29, 0x2c}, {0x55, 0x58},
                                                {0x61, 0x64}, {0x69, 0x6c}, {0xbd, 0xc0}};

void AppendPlayer(std::vector<std::uint8_t>& out, const GamePlayerSetup* player) {
    const std::uintptr_t none[3] = {};
    std::vector<std::uint8_t> snapshot = CopySnapshot(player, player, none);
    snapshot[0x18] = snapshot[0xbc] = 0;
    for (const auto& range : kUnwrittenPadding) {
        std::fill(snapshot.begin() + range[0], snapshot.begin() + range[1], std::uint8_t{0});
    }
    out.insert(out.end(), snapshot.begin(), snapshot.end());
}

void AppendVectorFields(std::vector<std::uint8_t>& out, const VectorBase& v) {
    VectorBase copy = v;
    copy.data = v.data != 0 ? 1 : 0;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&copy);
    out.insert(out.end(), bytes, bytes + sizeof(copy));
}

// `inBlock`: a team in a vector's block, whose unk04 the copy constructor
// leaves as heap leftovers (left out).
void AppendTeam(std::vector<std::uint8_t>& out, const CTeam* team, bool inBlock = false) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(team);
    out.insert(out.end(), bytes, bytes + (inBlock ? 4 : 8));
    AppendVectorFields(out, team->player_ids);
    for (std::int32_t i = 0; i < team->player_ids.filled && team->player_ids.data != 0; ++i) {
        const auto* id = reinterpret_cast<const std::uint8_t*>(Address(team->player_ids.data) + 4u * i);
        out.insert(out.end(), id, id + 4);
    }
    AppendVectorFields(out, team->players);
    for (std::int32_t i = 0; i < team->players.filled && team->players.data != 0; ++i) {
        AppendPlayer(out, reinterpret_cast<const GamePlayerSetup*>(Address(team->players.data) + 0xccu * i));
    }
}

template <typename AppendElement>
std::vector<std::uint8_t> VectorSnapshot(const VectorBase& v, std::uint32_t size, AppendElement append) {
    std::vector<std::uint8_t> out;
    AppendVectorFields(out, v);
    for (std::int32_t i = 0; i < v.filled && v.data != 0; ++i) {
        append(out, Address(v.data) + size * static_cast<std::uint32_t>(i));
    }
    return out;
}

std::vector<std::uint8_t> PlayersSnapshot(const VectorBase& v) {
    return VectorSnapshot(v, 0xcc, [](std::vector<std::uint8_t>& out, std::uintptr_t at) {
        AppendPlayer(out, reinterpret_cast<const GamePlayerSetup*>(at));
    });
}

std::vector<std::uint8_t> TeamsSnapshot(const VectorBase& v) {
    return VectorSnapshot(v, sizeof(CTeam), [](std::vector<std::uint8_t>& out, std::uintptr_t at) {
        AppendTeam(out, reinterpret_cast<const CTeam*>(at), true);
    });
}

using TeamConstructIdFn = CTeam*(__fastcall*)(CTeam* self, void* edx, std::int32_t id);
using TeamDestroyFn = void(__fastcall*)(CTeam* self, void* edx);
const auto kTeamConstructId = reinterpret_cast<TeamConstructIdFn>(0x004a02d0);
const auto kTeamDestroy = reinterpret_cast<TeamDestroyFn>(0x004a04d0);

// A team with a few sorted ids (capacity 10) and up to 3 player setups.
void FillTeam(test::Rng& rng, CTeam* team) {
    kTeamConstructId(team, nullptr, rng.Chance(1, 4) ? -1 : static_cast<std::int32_t>(rng.Below(20)));
    team->unk04 = rng.Next32();
    const std::uint32_t ids = rng.Below(team->player_ids.capacity + 1);
    std::int32_t id = static_cast<std::int32_t>(rng.Below(5)) - 2;
    for (std::uint32_t i = 0; i < ids; ++i) {
        reinterpret_cast<std::int32_t*>(Address(team->player_ids.data))[i] = id;
        id += static_cast<std::int32_t>(rng.Below(4));
    }
    team->player_ids.filled = static_cast<std::int32_t>(ids);
    team->player_ids.length = ids;
    const std::uint32_t players = rng.Below(4);
    for (std::uint32_t i = 0; i < players; ++i) {
        FillSetupForCopy(rng, reinterpret_cast<GamePlayerSetup*>(Address(team->players.data) + 0xccu * i));
    }
    team->players.filled = static_cast<std::int32_t>(players);
    team->players.length = rng.Below(players + 1);
}

void DestroyPlayers(VectorBase& v) {
    for (std::int32_t i = 0; i < v.filled; ++i) {
        GamePlayerSetup_Destruct(reinterpret_cast<void*>(Address(v.data) + 0xccu * i), nullptr);
    }
    msvc6::GameOperatorDelete(reinterpret_cast<void*>(Address(v.data)));
    v.data = 0;
    v.filled = 0;
}

void DestroyTeams(VectorBase& v) {
    for (std::int32_t i = 0; i < v.filled; ++i) {
        kTeamDestroy(reinterpret_cast<CTeam*>(Address(v.data) + sizeof(CTeam) * i), nullptr);
    }
    msvc6::GameOperatorDelete(reinterpret_cast<void*>(Address(v.data)));
    v.data = 0;
    v.filled = 0;
}

// A vector of `filled` (<= capacity) records; length <= filled, or ==
// filled (`sameLength`, as the game's ordered vectors keep them: an insert
// with length < filled would count a never-constructed element as filled).
void BuildRecords(test::Rng& rng, VectorBase& v, bool teams, bool sameLength = false) {
    const std::uint32_t size = teams ? sizeof(CTeam) : 0xcc;
    v.vtable = teams ? vtables::kWCBareVectorBase_int : vtables::kWCValOrderedVector_CPlayer;  // (not called)
    v.unk04 = rng.Next32();
    v.capacity = rng.Below(5);
    v.filled = static_cast<std::int32_t>(rng.Below(v.capacity + 1));
    v.length = rng.Chance(2, 3) ? static_cast<std::uint32_t>(v.filled) : rng.Below(static_cast<std::uint32_t>(v.filled) + 1);
    if (sameLength) {
        v.length = static_cast<std::uint32_t>(v.filled);
    }
    v.grow = rng.Chance(1, 5) ? 0 : rng.Below(4);
    v.data = v.capacity != 0 ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
                                   msvc6::GameOperatorNew(v.capacity * size)))
                             : 0;
    for (std::int32_t i = 0; i < v.filled; ++i) {
        const std::uintptr_t at = Address(v.data) + size * static_cast<std::uint32_t>(i);
        if (teams) {
            FillTeam(rng, reinterpret_cast<CTeam*>(at));
        } else {
            FillSetupForCopy(rng, reinterpret_cast<GamePlayerSetup*>(at));
        }
    }
}

struct VectorFns {
    VecResizeFn playerResize, teamResize;
    VecInsertAtFn playerInsert, teamInsert;
    VecCopyContentsFn copyContents;
    TeamCopyHookFn teamCopy, teamAssign;
    AddPlayerIdHookFn addPlayerId;
    LogSlotHookFn logSlot;
};

VectorFns OriginalVectorFns() {
    return {Original_PlayerVector_Resize,     Original_TeamVector_Resize,   Original_PlayerVector_InsertAt,
            Original_TeamVector_InsertAt,     Original_PlayerBareVector_CopyContents, Original_CTeam_CopyConstruct,
            Original_CTeam_Assign,            Original_CTeam_AddPlayerId,   Original_GamePlayerSetup_LogSlot};
}

VectorFns OurVectorFns() {
    return {&PlayerVector_Resize,  &TeamVector_Resize, &PlayerVector_InsertAt, &TeamVector_InsertAt,
            &PlayerBareVector_CopyContents, &CTeam_CopyConstruct, &CTeam_Assign, &CTeam_AddPlayerId,
            &GamePlayerSetup_LogSlot};
}

struct VectorOutcome {
    std::uint32_t eax = 0;
    std::vector<std::uint8_t> state;
    std::vector<DebugLogCall> log;
    bool operator==(const VectorOutcome& o) const { return eax == o.eax && state == o.state && log == o.log; }
};

// One case of operation `op` on one side.
VectorOutcome RunVectorCase(const VectorFns& fns, int op, std::uint64_t seed) {
    test::Rng rng(seed);
    VectorOutcome out;
    const bool teams = (op & 1) != 0;
    switch (op) {
        case 0:
        case 1: {  // Resize
            VectorBase v{};
            BuildRecords(rng, v, teams);
            const std::uint32_t n = rng.Below(7);
            out.eax = (teams ? fns.teamResize : fns.playerResize)(&v, nullptr, n);
            out.state = teams ? TeamsSnapshot(v) : PlayersSnapshot(v);
            teams ? DestroyTeams(v) : DestroyPlayers(v);
            break;
        }
        case 2:
        case 3: {  // InsertAt
            VectorBase v{};
            BuildRecords(rng, v, teams, true);
            alignas(4) std::uint8_t value[sizeof(GamePlayerSetup)];
            if (teams) {
                FillTeam(rng, reinterpret_cast<CTeam*>(value));
            } else {
                FillSetupForCopy(rng, reinterpret_cast<GamePlayerSetup*>(value));
            }
            const std::int32_t index = static_cast<std::int32_t>(rng.Below(v.length + 5)) - 2;
            out.eax = (teams ? fns.teamInsert : fns.playerInsert)(&v, nullptr, index, value);
            out.state = teams ? TeamsSnapshot(v) : PlayersSnapshot(v);
            if (teams) {
                AppendTeam(out.state, reinterpret_cast<CTeam*>(value));
                kTeamDestroy(reinterpret_cast<CTeam*>(value), nullptr);
                DestroyTeams(v);
            } else {
                AppendPlayer(out.state, reinterpret_cast<GamePlayerSetup*>(value));
                GamePlayerSetup_Destruct(value, nullptr);
                DestroyPlayers(v);
            }
            break;
        }
        case 4: {  // the player vector's contents copy
            VectorBase from{};
            BuildRecords(rng, from, false);
            VectorBase to;
            rng.Fill(&to, sizeof(to));
            fns.copyContents(&to, nullptr, &from);
            out.state = PlayersSnapshot(to);
            const std::vector<std::uint8_t> source = PlayersSnapshot(from);
            out.state.insert(out.state.end(), source.begin(), source.end());
            DestroyPlayers(to);
            DestroyPlayers(from);
            break;
        }
        case 5:
        case 6: {  // CTeam copy constructor / operator=
            alignas(4) std::uint8_t fromBytes[sizeof(CTeam)], toBytes[sizeof(CTeam)];
            auto* from = reinterpret_cast<CTeam*>(fromBytes);
            auto* to = reinterpret_cast<CTeam*>(toBytes);
            FillTeam(rng, from);
            const bool self = op == 6 && rng.Chance(1, 8);
            if (op == 6) {
                FillTeam(rng, to);
            } else {
                rng.Fill(toBytes, sizeof(toBytes));
            }
            CTeam* result = (op == 5 ? fns.teamCopy : fns.teamAssign)(to, nullptr, self ? to : from);
            out.eax = result == to;
            AppendTeam(out.state, to);
            AppendTeam(out.state, from);
            kTeamDestroy(to, nullptr);
            kTeamDestroy(from, nullptr);
            break;
        }
        case 7: {  // AddPlayerId (the real sorted vector's find)
            alignas(4) std::uint8_t teamBytes[sizeof(CTeam)];
            auto* team = reinterpret_cast<CTeam*>(teamBytes);
            FillTeam(rng, team);
            team->player_ids.grow = rng.Chance(1, 4) ? 0 : rng.Below(3);
            const std::int32_t id = static_cast<std::int32_t>(rng.Below(30)) - 4;
            out.eax = fns.addPlayerId(team, nullptr, id);
            AppendTeam(out.state, team);
            kTeamDestroy(team, nullptr);
            break;
        }
        case 8: {  // LogSlot
            alignas(4) std::uint8_t bytes[sizeof(GamePlayerSetup)];
            auto* player = reinterpret_cast<GamePlayerSetup*>(bytes);
            FillSetupForCopy(rng, player);
            {
                DebugLogRecorder recorder;
                out.eax = fns.logSlot(player, nullptr);
                out.log = recorder.Take();
            }
            // The text pointer itself differs between the sides: compare the text.
            for (DebugLogCall& call : out.log) {
                if (call.count == 8) {
                    const char* text = reinterpret_cast<const char*>(static_cast<std::uintptr_t>(call.args[7]));
                    call.args[7] = static_cast<std::uint32_t>(std::strlen(text));
                    out.state.assign(text, text + std::strlen(text));
                }
            }
            GamePlayerSetup_Destruct(player, nullptr);
            break;
        }
    }
    return out;
}

constexpr int kVectorOps = 9;

void CompareVectors(test::Context& ctx, const VectorFns& candidate, int casesPerOp) {
    const VectorFns original = OriginalVectorFns();
    for (int op = 0; op < kVectorOps; ++op) {
        for (int i = 0; i < casesPerOp; ++i) {
            const std::uint64_t seed = ctx.rng().Next64();
            const VectorOutcome a = RunVectorCase(original, op, seed);
            const VectorOutcome b = RunVectorCase(candidate, op, seed);
            if (!S3RE_EXPECT(a.log.size() == b.log.size()) || !S3RE_EXPECT_EQ(a.eax, b.eax) || !S3RE_EXPECT(a == b)) {
                std::size_t at = 0;
                while (at < a.state.size() && at < b.state.size() && a.state[at] == b.state[at]) {
                    ++at;
                }
                ctx.Fail(__FILE__, __LINE__, "  op %d, case %d: state sizes %zu/%zu, first difference at %zu (%02x/%02x)",
                         op, i, a.state.size(), b.state.size(), at, at < a.state.size() ? a.state[at] : 0,
                         at < b.state.size() ? b.state[at] : 0);
                return;
            }
        }
    }
}

// Wrong implementations, one per function (each must be caught).
std::uint32_t __fastcall WrongPlayerResize(VectorBase* self, void* edx, std::uint32_t n) {
    const std::uint32_t result = PlayerVector_Resize(self, edx, n);
    self->length = self->capacity;  // the length is left alone in the original
    return result;
}
std::uint32_t __fastcall WrongTeamResize(VectorBase* self, void* edx, std::uint32_t n) {
    return TeamVector_Resize(self, edx, n + (n == 2 ? 1 : 0));
}
std::uint32_t __fastcall WrongPlayerInsert(VectorBase* self, void* edx, std::int32_t index, const void* value) {
    return PlayerVector_InsertAt(self, edx, index < 0 ? static_cast<std::int32_t>(self->length) : index, value);
}
std::uint32_t __fastcall WrongTeamInsert(VectorBase* self, void* edx, std::int32_t index, const void* value) {
    return TeamVector_InsertAt(self, edx, index > 0 ? index - 1 : index, value);
}
void __fastcall WrongCopyContents(VectorBase* self, void* edx, const VectorBase* from) {
    PlayerBareVector_CopyContents(self, edx, from);
    self->unk04 = 0;
}
CTeam* __fastcall WrongTeamCopy(CTeam* self, void* edx, const CTeam* from) {
    CTeam_CopyConstruct(self, edx, from);
    self->unk04 = from->unk04;  // not copied by the original
    return self;
}
CTeam* __fastcall WrongTeamAssign(CTeam* self, void* edx, const CTeam* from) {
    CTeam_Assign(self, edx, from);
    self->players.length = self->players.filled;
    return self;
}
std::uint32_t __fastcall WrongAddPlayerId(CTeam* self, void* edx, std::int32_t id) {
    return CTeam_AddPlayerId(self, edx, id + 1);
}
std::uint32_t __fastcall WrongLogSlot(const GamePlayerSetup* self, void* edx) {
    return GamePlayerSetup_LogSlot(self, edx) + 1;
}

}  // namespace

S3RE_TEST(GameSettingsVectors_MatchOriginal) {
    CompareVectors(ctx, OurVectorFns(), 300);
}
S3RE_TEST(GameSettingsVectors_ComparisonDetectsWrongImplementation) {
    const VectorFns ours = OurVectorFns();
    for (int k = 0; k < kVectorOps; ++k) {
        VectorFns wrong = ours;
        switch (k) {
            case 0: wrong.playerResize = &WrongPlayerResize; break;
            case 1: wrong.teamResize = &WrongTeamResize; break;
            case 2: wrong.playerInsert = &WrongPlayerInsert; break;
            case 3: wrong.teamInsert = &WrongTeamInsert; break;
            case 4: wrong.copyContents = &WrongCopyContents; break;
            case 5: wrong.teamCopy = &WrongTeamCopy; break;
            case 6: wrong.teamAssign = &WrongTeamAssign; break;
            case 7: wrong.addPlayerId = &WrongAddPlayerId; break;
            case 8: wrong.logSlot = &WrongLogSlot; break;
        }
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        CompareVectors(probe, wrong, 60);
        if (!S3RE_EXPECT(probe.failures() > 0)) {
            ctx.Fail(__FILE__, __LINE__, "  wrong implementation %d not caught", k);
        }
    }
}

S3RE_TEST(GamePlayerSetup_Set_MatchesOriginal) {
    CompareSetupSet(ctx, &GamePlayerSetup_Set, 3000);
}
S3RE_TEST(GamePlayerSetup_Set_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetupSet(probe, &WrongSet, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GamePlayerSetup_CopyConstruct_MatchesOriginal) {
    CompareSetupCopy(ctx, &GamePlayerSetup_CopyConstruct, false, 1500);
}
S3RE_TEST(GamePlayerSetup_CopyConstruct_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetupCopy(probe, &WrongCopyConstruct, false, 100);
    S3RE_EXPECT(probe.failures() > 0);
}

S3RE_TEST(GamePlayerSetup_Assign_MatchesOriginal) {
    CompareSetupCopy(ctx, &GamePlayerSetup_Assign, true, 1500);
}
S3RE_TEST(GamePlayerSetup_Assign_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareSetupCopy(probe, &WrongAssign, true, 100);
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
S3RE_TEST(GameSettings_PlayerSetupById_MatchesOriginal) {
    CompareAccessById(ctx, &GameSettings_PlayerSetupById, &GameSettings_HasPlayerId, 3000);
}
S3RE_TEST(GameSettings_PlayerSetupById_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareAccessById(probe, &WrongPlayerSetupById, &GameSettings_HasPlayerId, 300);
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
