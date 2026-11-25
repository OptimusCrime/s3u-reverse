// Tests of sound_data.cpp (CSoundData: constructor, destructors, Close,
// ReleaseBuffers, Restore, the volume, DetachFromMedium,
// FindDirectoryContainingFile and Open).
//
// Lifetime cases (constructor, destructors, Close, ReleaseBuffers,
// Restore, volume): two objects over the same random bytes, constructed by
// the original and by ours and compared (pointers as "set or not", the
// dictionaries' header pointers relative to the object); then a random
// state (views, a fake IDirectSound or none, a real CPrimaryBuffer with a
// fake buffer or none, 0..4 effects in the dictionary, enabled) and one
// function under scripted imports (FakeWin32: UnmapViewOfFile,
// CloseHandle) and a scripted DirectSound (FakeDSound). The effects are
// game-heap blocks holding an id; ~CSoundEffect (0x5c2b00) and
// CSoundEffect::Restore (0x5c2d20) are patched to stubs that log the id
// (Restore throws a CDirectSoundException at a scripted call). Compared:
// the logs, DebugLog, the exception, the object.
// FindDirectoryContainingFile and DetachFromMedium: as the graphics
// loader's tests (gfx_loader_tests.cpp).
// Open: the real SND folder (read only), every primary format 0..5, detach
// modes 0, 2, 3, scripted DirectSound results (missing driver, busy card,
// no priority level, unsupported format, failing calls), the object's
// vtable a fake one (the lost-buffer path calls its slot 1). Compared:
// the object's fields (folder text, sizes, views set, tables relative to
// their views, unk148, enabled, format, the primary buffer's fields), the
// DirectSound log, DebugLog and the exception. Both objects are destroyed
// by the original destructor afterwards.

#include "game/debug_log.h"
#include "game/framework/directx_sound.h"
#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/sound/sound_layout.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"
#include "test/fake_dsound.h"
#include "test/fake_win32.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using msvc6::String;

void* __fastcall CSoundData_Construct(void* self, void* edx);
void* __fastcall CSoundData_DeletingDestroy(void* self, void* edx, std::uint32_t flags);
std::uint32_t __fastcall CSoundData_Destroy(void* self, void* edx);
void __fastcall CSoundData_Close(void* self, void* edx);
void __fastcall CSoundData_ReleaseBuffers(void* self, void* edx);
void __fastcall CSoundData_Restore(void* self, void* edx);
void __fastcall CSoundData_SetVolume(void* self, void* edx, std::int32_t volume);
std::int32_t __fastcall CSoundData_GetVolume(void* self, void* edx);
void __fastcall CSoundData_DetachFromMedium(void* self, void* edx, std::uint32_t otherwise);
String* __fastcall CSoundData_FindDirectoryContainingFile(void* self, void* edx, String* result, void* folders,
                                                         const String* file);
void __fastcall CSoundData_Open(void* self, void* edx, std::uint32_t window, const String* basePath,
                                std::uint32_t detachMode, std::uint32_t format);

using SndConstructFn = decltype(&CSoundData_Construct);
using SndDeletingFn = decltype(&CSoundData_DeletingDestroy);
using SndDestroyFn = decltype(&CSoundData_Destroy);
using SndVoidFn = decltype(&CSoundData_Close);
using SndSetVolumeFn = decltype(&CSoundData_SetVolume);
using SndGetVolumeFn = decltype(&CSoundData_GetVolume);
using SndDetachFn = decltype(&CSoundData_DetachFromMedium);
using SndFindDirectoryFn = decltype(&CSoundData_FindDirectoryContainingFile);
using SndOpenFn = decltype(&CSoundData_Open);

extern SndConstructFn Original_CSoundData_Construct;
extern SndDeletingFn Original_CSoundData_DeletingDestroy;
extern SndDestroyFn Original_CSoundData_Destroy;
extern SndVoidFn Original_CSoundData_Close;
extern SndVoidFn Original_CSoundData_ReleaseBuffers;
extern SndVoidFn Original_CSoundData_Restore;
extern SndSetVolumeFn Original_CSoundData_SetVolume;
extern SndGetVolumeFn Original_CSoundData_GetVolume;
extern SndDetachFn Original_CSoundData_DetachFromMedium;
extern SndFindDirectoryFn Original_CSoundData_FindDirectoryContainingFile;
extern SndOpenFn Original_CSoundData_Open;

namespace {

using sound::CSoundData;
using test::CaughtException;
using test::FakeDSound;
using test::FakeWin32;
using test::FakeWin32Config;

struct Functions {
    SndConstructFn construct;
    SndDeletingFn deleting;
    SndDestroyFn destroy;
    SndVoidFn close;
    SndVoidFn releaseBuffers;
    SndVoidFn restore;
    SndSetVolumeFn setVolume;
    SndGetVolumeFn getVolume;
    SndDetachFn detach;
    SndFindDirectoryFn find;
    SndOpenFn open;
};

Functions Originals() {
    return {Original_CSoundData_Construct, Original_CSoundData_DeletingDestroy, Original_CSoundData_Destroy,
            Original_CSoundData_Close,     Original_CSoundData_ReleaseBuffers,  Original_CSoundData_Restore,
            Original_CSoundData_SetVolume, Original_CSoundData_GetVolume,       Original_CSoundData_DetachFromMedium,
            Original_CSoundData_FindDirectoryContainingFile, Original_CSoundData_Open};
}

Functions Ours() {
    return {&CSoundData_Construct, &CSoundData_DeletingDestroy, &CSoundData_Destroy,
            &CSoundData_Close,     &CSoundData_ReleaseBuffers,  &CSoundData_Restore,
            &CSoundData_SetVolume, &CSoundData_GetVolume,       &CSoundData_DetachFromMedium,
            &CSoundData_FindDirectoryContainingFile, &CSoundData_Open};
}

constexpr std::uintptr_t kStringVectorConstruct = 0x005fda00;
constexpr std::uintptr_t kStringVectorInsertAt = 0x004148a0;
constexpr std::uintptr_t kStringVectorDestroy = 0x00409d50;
constexpr std::uintptr_t kDictFind = 0x0060bab0;
constexpr std::uintptr_t kDictInsert = 0x0060bbe0;
constexpr std::uintptr_t kPrimaryConstruct = 0x005c2180;
constexpr std::uintptr_t kEffectDestroy = 0x005c2b00;
constexpr std::uintptr_t kEffectRestore = 0x005c2d20;

struct StringVector {
    std::uint32_t vtable;
    std::uint32_t unk04;
    String* data;
    std::uint32_t capacity;
    std::int32_t filled;
    std::uint32_t length;
    std::uint32_t grow;
};

std::string Text(const String& s) {
    return s.ptr != nullptr ? std::string(s.ptr, s.len) : std::string();
}

// --------------------------------------------------------------------------
// Stubs for the effects' methods
// --------------------------------------------------------------------------

struct EffectLog {
    std::vector<std::uint32_t> calls;
    int throwAt = -1;  // Restore call number that throws
    int restores = 0;
};
EffectLog* g_effects = nullptr;

void __fastcall StubEffectDestroy(void* effect, void*) {
    g_effects->calls.push_back(0xde000000u | *static_cast<std::uint32_t*>(effect));
}

void __fastcall StubEffectRestore(void* effect, void*) {
    g_effects->calls.push_back(0x7e000000u | *static_cast<std::uint32_t*>(effect));
    if (g_effects->restores++ == g_effects->throwAt) {
        DirectX_CheckDSResult(nullptr, nullptr, static_cast<std::int32_t>(0x88780078), "test");
    }
}

// --------------------------------------------------------------------------
// Guarded calls (no C++ objects in the __try frames)
// --------------------------------------------------------------------------

enum class Op { Destroy, Deleting, Close, ReleaseBuffers, Restore, SetVolume, GetVolume, Detach, Find, Open };

struct Call {
    Op op;
    void* self;
    std::uint32_t a;
    String* result;
    void* folders;
    const String* file;
    const String* basePath;
    std::uint32_t b;
    std::uint32_t c;
};

std::uint32_t Dispatch(const Functions* f, const Call* call) {
    switch (call->op) {
        case Op::Destroy: return f->destroy(call->self, nullptr);
        case Op::Deleting: return reinterpret_cast<std::uint32_t>(f->deleting(call->self, nullptr, call->a));
        case Op::Close: f->close(call->self, nullptr); return 0;
        case Op::ReleaseBuffers: f->releaseBuffers(call->self, nullptr); return 0;
        case Op::Restore: f->restore(call->self, nullptr); return 0;
        case Op::SetVolume: f->setVolume(call->self, nullptr, static_cast<std::int32_t>(call->a)); return 0;
        case Op::GetVolume: return static_cast<std::uint32_t>(f->getVolume(call->self, nullptr));
        case Op::Detach: f->detach(call->self, nullptr, call->a); return 0;
        case Op::Find:
            return reinterpret_cast<std::uint32_t>(f->find(call->self, nullptr, call->result, call->folders, call->file));
        case Op::Open: f->open(call->self, nullptr, call->a, call->basePath, call->b, call->c); return 0;
    }
    return 0;
}

struct Outcome {
    std::uint32_t eax = 0;
    CaughtException caught;
};

Outcome Run(const Functions* f, const Call* call) {
    Outcome out;
    __try {
        out.eax = Dispatch(f, call);
    } __except (test::CatchGameException(GetExceptionInformation(), &out.caught)) {
    }
    return out;
}

// --------------------------------------------------------------------------
// Object comparison
// --------------------------------------------------------------------------

// The object with its pointers made comparable: owned blocks as "set or
// not", the dictionaries' header pointers relative to the object, node
// pointers as "set or not".
void Normalize(CSoundData* s, const CSoundData* original) {
    const auto base = reinterpret_cast<std::uint32_t>(original);
    s->folder.ptr = reinterpret_cast<char*>(s->folder.ptr != nullptr ? 1 : 0);
    s->folder.allocator = 0;  // known difference (constructor)
    for (std::size_t i = 0; i < sound::kFiles; ++i) {
        s->mappings[i] = s->mappings[i] != 0;
    }
    // The node levels are random (the skip list's level generator runs on
    // global state that the two objects' inserts consume differently):
    // only level 0 is compared.
    for (fileformats::SkipListDict* dict : {&s->effects, &s->lastUse}) {
        dict->header_ptr -= base;
        dict->header[0] = dict->header[0] != 0;
        for (std::size_t level = 1; level < 20; ++level) {
            dict->header[level] = 0;
        }
        dict->top_level = 0;
    }
    s->primary = reinterpret_cast<void*>(s->primary != nullptr ? 1 : 0);
}

bool SameObject(test::Context& ctx, const CSoundData* a, const CSoundData* b) {
    CSoundData ca;
    CSoundData cb;
    std::memcpy(&ca, a, sizeof(ca));
    std::memcpy(&cb, b, sizeof(cb));
    Normalize(&ca, a);
    Normalize(&cb, b);
    bool same = S3RE_EXPECT_MEM_EQ(&ca, &cb, sizeof(ca));
    same &= S3RE_EXPECT(Text(a->folder) == Text(b->folder));
    if (a->primary != nullptr && b->primary != nullptr) {
        same &= S3RE_EXPECT_MEM_EQ(a->primary, b->primary, sizeof(framework::CDirectSoundBuffer));
    }
    return same;
}

// --------------------------------------------------------------------------
// Lifetime and the small methods
// --------------------------------------------------------------------------

struct State {
    std::uint32_t views[sound::kFiles];
    bool sound;
    bool primary;
    void* primaryBuffer;
    std::uint32_t createFlags;
    std::uint32_t effectKeys[4];
    std::size_t effects;
    bool enabled;
};

State RandomState(test::Rng& rng) {
    State st{};
    for (std::size_t i = 0; i < sound::kFiles; ++i) {
        st.views[i] = rng.Chance(1, 2) ? (rng.Next32() | 1) : 0;
    }
    st.sound = rng.Chance(2, 3);
    st.primary = rng.Chance(2, 3);
    // Never null: ReleaseBuffers releases it unchecked (the original too).
    st.primaryBuffer = FakeDSound::Buffer(rng.Below(test::kFakeSoundBuffers));
    st.createFlags = rng.Next32();
    st.effects = rng.Below(5);
    for (std::size_t i = 0; i < st.effects; ++i) {
        st.effectKeys[i] = rng.Next32();
    }
    st.enabled = rng.Chance(2, 3);
    return st;
}

using DictFindFn = std::uint32_t(__fastcall*)(void* dict, void* edx, const std::uint32_t* key);

// dict[key] = value, as GetEffect's inlined operator[] does it.
void DictSet(fileformats::SkipListDict* dict, std::uint32_t key, std::uint32_t value) {
    dict->key_slot = key;
    std::uint32_t node = reinterpret_cast<DictFindFn>(kDictFind)(dict, nullptr, &dict->key_slot);
    if (node == 0) {
        node = reinterpret_cast<DictFindFn>(kDictInsert)(dict, nullptr, &dict->key_slot);
    }
    *reinterpret_cast<std::uint32_t*>(node - 4) = value;
}

void ApplyState(CSoundData* s, const State& st) {
    for (std::size_t i = 0; i < sound::kFiles; ++i) {
        s->views[i] = st.views[i];
        s->tables[i] = st.views[i] + 0x1c;
    }
    s->base.sound = st.sound ? FakeDSound::Sound() : nullptr;
    if (st.primary) {
        void* const primary = msvc6::GameOperatorNew(0xc);
        reinterpret_cast<void*(__fastcall*)(void*, void*)>(kPrimaryConstruct)(primary, nullptr);
        auto* const buffer = static_cast<framework::CDirectSoundBuffer*>(primary);
        buffer->buffer = st.primaryBuffer;
        buffer->createFlags = st.createFlags;
        s->primary = primary;
    }
    for (std::size_t i = 0; i < st.effects; ++i) {
        auto* const effect = static_cast<std::uint32_t*>(msvc6::GameOperatorNew(0x10));
        effect[0] = static_cast<std::uint32_t>(i + 1);
        DictSet(&s->effects, st.effectKeys[i], reinterpret_cast<std::uint32_t>(effect));
    }
    if (st.effects > 0 && st.effectKeys[0] != 0) {
        DictSet(&s->lastUse, st.effectKeys[0], 1234);
    }
    s->enabled = st.enabled ? 1 : 0;
}

// The objects' fake vtable (Disconnect calls slot 2: ours or the original
// ReleaseBuffers, whichever is under test, through the real vtable).
bool CompareLifetime(test::Context& ctx, const Functions& ours) {
    test::Rng& rng = ctx.rng();
    auto* a = static_cast<CSoundData*>(msvc6::GameOperatorNew(sizeof(CSoundData)));
    auto* b = static_cast<CSoundData*>(msvc6::GameOperatorNew(sizeof(CSoundData)));
    rng.Fill(a, sizeof(*a));
    std::memcpy(b, a, sizeof(*a));
    void* const savedSound = g_directSound.get();
    Original_CSoundData_Construct(a, nullptr);
    const std::uint32_t soundA = reinterpret_cast<std::uint32_t>(g_directSound.get()) == reinterpret_cast<std::uint32_t>(a);
    ours.construct(b, nullptr);
    const std::uint32_t soundB = reinterpret_cast<std::uint32_t>(g_directSound.get()) == reinterpret_cast<std::uint32_t>(b);
    bool same = S3RE_EXPECT_EQ(soundA, soundB);
    // createdTime comes from the clock: copy a's (the call is compared by
    // the emulator spec).
    b->createdTime = a->createdTime;
    same &= SameObject(ctx, a, b);

    const State st = RandomState(rng);
    ApplyState(a, st);
    ApplyState(b, st);
    static const Op kOps[] = {Op::Destroy, Op::Deleting, Op::Close, Op::ReleaseBuffers,
                              Op::Restore, Op::SetVolume, Op::GetVolume};
    const Op op = kOps[rng.Below(7)];
    Call ca{op, a, rng.Chance(1, 2) ? rng.Below(2) : rng.Next32(), nullptr, nullptr, nullptr, nullptr, 0, 0};
    Call cb = ca;
    cb.self = b;
    const std::uint64_t seed = rng.Next64();
    test::FakeDSoundConfig dsConfig;
    dsConfig.failChance = rng.Below(4);
    FakeDSound da(seed, dsConfig);
    FakeDSound db(seed, dsConfig);
    const std::uint32_t imports = test::kFakeUnmapView | test::kFakeCloseHandle;
    FakeWin32 wa(seed, imports);
    FakeWin32 wb(seed, imports);
    EffectLog ea;
    EffectLog eb;
    ea.throwAt = eb.throwAt = rng.Chance(1, 2) ? static_cast<int>(rng.Below(4)) : -1;
    std::vector<DebugLogCall> logA;
    std::vector<DebugLogCall> logB;
    const Functions originals = Originals();

    test::PatchJump(kEffectDestroy, reinterpret_cast<const void*>(&StubEffectDestroy));
    test::PatchJump(kEffectRestore, reinterpret_cast<const void*>(&StubEffectRestore));
    FakeDSound::ResetObjects();
    g_directSound = FakeDSound::Game(0);
    g_effects = &ea;
    da.Install();
    wa.Install();
    SetDebugLogCapture(&logA);
    const Outcome oa = Run(&originals, &ca);
    SetDebugLogCapture(nullptr);
    wa.Uninstall();
    da.Uninstall();
    FakeDSound::ResetObjects();
    g_directSound = FakeDSound::Game(0);
    g_effects = &eb;
    db.Install();
    wb.Install();
    SetDebugLogCapture(&logB);
    const Outcome ob = Run(&ours, &cb);
    SetDebugLogCapture(nullptr);
    wb.Uninstall();
    db.Uninstall();
    g_effects = nullptr;
    test::UnpatchJump(kEffectRestore);
    test::UnpatchJump(kEffectDestroy);
    const bool soundCleared = g_directSound.get() == nullptr;
    g_directSound = savedSound;

    same &= S3RE_EXPECT(oa.caught == ob.caught);
    same &= S3RE_EXPECT_EQ(op == Op::GetVolume ? oa.eax : 0u, op == Op::GetVolume ? ob.eax : 0u);
    same &= S3RE_EXPECT(da.log_ == db.log_);
    same &= S3RE_EXPECT(wa.log_ == wb.log_);
    same &= S3RE_EXPECT(ea.calls == eb.calls);
    same &= S3RE_EXPECT(logA == logB);
    const bool destroyed = op == Op::Destroy || op == Op::Deleting;
    if (!destroyed) {
        same &= SameObject(ctx, a, b);
    }
    if (!same) {
        ctx.Fail(__FILE__, __LINE__, "  op %d, effects %u, throwAt %d, cleared %d", static_cast<int>(op),
                 static_cast<unsigned>(st.effects), ea.throwAt, soundCleared ? 1 : 0);
    }
    // Clean up what the call left (the effect blocks are leaked when the
    // function didn't delete them; they are tiny).
    if (!destroyed) {
        Call cleanup{Op::Deleting, a, 1, nullptr, nullptr, nullptr, nullptr, 0, 0};
        FakeDSound fc(1, {0});
        FakeWin32 wc(1, imports);
        test::PatchJump(kEffectDestroy, reinterpret_cast<const void*>(&StubEffectDestroy));
        EffectLog ec;
        g_effects = &ec;
        fc.Install();
        wc.Install();
        a->enabled = b->enabled = 0;
        for (CSoundData* s : {a, b}) {
            // A released primary buffer would be released again (unchecked).
            if (s->primary != nullptr && static_cast<framework::CDirectSoundBuffer*>(s->primary)->buffer == nullptr) {
                static_cast<framework::CDirectSoundBuffer*>(s->primary)->buffer = FakeDSound::Buffer(0);
            }
        }
        Run(&originals, &cleanup);
        cleanup.self = b;
        Run(&originals, &cleanup);
        wc.Uninstall();
        fc.Uninstall();
        g_effects = nullptr;
        test::UnpatchJump(kEffectDestroy);
        g_directSound = savedSound;
    } else if (op == Op::Destroy) {
        msvc6::GameOperatorDelete(a);
        msvc6::GameOperatorDelete(b);
    } else if ((ca.a & 1) == 0) {
        msvc6::GameOperatorDelete(a);
        msvc6::GameOperatorDelete(b);
    }
    return same;
}

// --------------------------------------------------------------------------
// FindDirectoryContainingFile
// --------------------------------------------------------------------------

void BuildFolders(StringVector* folders, test::Rng& rng, int count) {
    reinterpret_cast<void*(__fastcall*)(void*, void*, std::uint32_t, std::uint32_t)>(kStringVectorConstruct)(
        folders, nullptr, 10, 5);
    static const char* const kFolders[] = {".", "Snd", ".\\Snd", "", "..", "D:\\S3\\Snd", "C:\\x"};
    for (int i = 0; i < count; ++i) {
        String folder{};
        msvc6::String_AssignText(&folder, nullptr, kFolders[rng.Below(7)]);
        reinterpret_cast<std::uint32_t(__fastcall*)(void*, void*, std::uint32_t, const String*)>(
            kStringVectorInsertAt)(folders, nullptr, folders->length, &folder);
        msvc6::String_Destroy(&folder, nullptr);
    }
}

void DestroyFolders(StringVector* folders) {
    reinterpret_cast<void(__fastcall*)(void*, void*)>(kStringVectorDestroy)(folders, nullptr);
}

bool CompareFind(test::Context& ctx, const Functions& ours) {
    test::Rng& rng = ctx.rng();
    const int count = static_cast<int>(rng.Below(7));
    const std::uint64_t folderSeed = rng.Next64();
    test::Rng ra(folderSeed);
    test::Rng rb(folderSeed);
    StringVector fa{};
    StringVector fb{};
    BuildFolders(&fa, ra, count);
    BuildFolders(&fb, rb, count);
    String file{};
    msvc6::String_AssignText(&file, nullptr, rng.Chance(1, 8) ? "" : "siedler3_00.dat");
    String resultA{};
    String resultB{};
    FakeWin32Config config;
    config.failChance = rng.Below(4);
    config.currentDirectory = rng.Chance(1, 6) ? "" : "C:\\Games\\S3\\Snd";
    const std::uint64_t seed = rng.Next64();
    const std::uint32_t imports = test::kFakeGetCurrentDirectory | test::kFakeSetCurrentDirectory |
                                  test::kFakeFindFirstFile | test::kFakeFindClose;
    FakeWin32 wa(seed, imports, config);
    FakeWin32 wb(seed, imports, config);
    const Functions originals = Originals();
    Call ca{Op::Find, nullptr, 0, &resultA, &fa, &file, nullptr, 0, 0};
    Call cb{Op::Find, nullptr, 0, &resultB, &fb, &file, nullptr, 0, 0};
    wa.Install();
    const Outcome oa = Run(&originals, &ca);
    wa.Uninstall();
    wb.Install();
    const Outcome ob = Run(&ours, &cb);
    wb.Uninstall();
    CaughtException caughtA = oa.caught;
    CaughtException caughtB = ob.caught;
    caughtA.code = caughtB.code = 0;  // GetLastError stays real (see gfx_loader_tests.cpp)
    bool same = S3RE_EXPECT(caughtA == caughtB);
    same &= S3RE_EXPECT(wa.log_ == wb.log_);
    if (!oa.caught.threw) {
        same &= S3RE_EXPECT_EQ(oa.eax == reinterpret_cast<std::uint32_t>(&resultA),
                               ob.eax == reinterpret_cast<std::uint32_t>(&resultB));
        same &= S3RE_EXPECT(Text(resultA) == Text(resultB));
        same &= S3RE_EXPECT_EQ(resultA.res, resultB.res);
        msvc6::String_Destroy(&resultA, nullptr);
        msvc6::String_Destroy(&resultB, nullptr);
    }
    same &= S3RE_EXPECT_EQ(fa.length, fb.length);
    same &= S3RE_EXPECT_EQ(fa.capacity, fb.capacity);
    for (std::uint32_t i = 0; i < fa.length && i < fb.length && same; ++i) {
        same &= S3RE_EXPECT(Text(fa.data[i]) == Text(fb.data[i]));
    }
    if (!same) {
        ctx.Fail(__FILE__, __LINE__, "  folders %d, fail chance %u", count, config.failChance);
    }
    DestroyFolders(&fa);
    DestroyFolders(&fb);
    msvc6::String_Destroy(&file, nullptr);
    return same;
}

// --------------------------------------------------------------------------
// DetachFromMedium
// --------------------------------------------------------------------------

bool CompareDetach(test::Context& ctx, const Functions& ours) {
    test::Rng& rng = ctx.rng();
    static void* const area = VirtualAlloc(nullptr, 0x40000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    const std::uint32_t low = reinterpret_cast<std::uint32_t>(area);
    static CSoundData a;
    static CSoundData b;
    rng.Fill(&a, sizeof(a));
    for (std::size_t i = 0; i < sound::kFiles; ++i) {
        a.views[i] = rng.Chance(2, 3) ? low + rng.Below(0x20000) : 0;
        a.fileSizes[i] = rng.Below(0x18000);
    }
    a.enabled = rng.Chance(3, 4) ? 1 : 0;
    std::memcpy(&b, &a, sizeof(a));
    const char* const folder = rng.Chance(1, 2) ? "D:\\Snd\\" : "C:\\S3\\Snd\\";
    msvc6::detail::Clear(&a.folder);
    msvc6::detail::Clear(&b.folder);
    msvc6::String_AssignText(&a.folder, nullptr, folder);
    msvc6::String_AssignText(&b.folder, nullptr, folder);
    FakeWin32Config config;
    config.regionLow = low;
    config.regionHigh = low + 0x40000;
    config.failChance = 0;
    static const std::uint32_t kDriveTypes[] = {2, 3, 4, 5, 0};
    config.driveType = kDriveTypes[rng.Below(5)];
    const std::uint32_t otherwise = rng.Next32();
    const std::uint64_t seed = rng.Next64();
    const std::uint32_t imports = test::kFakeGetDriveType | test::kFakeGetSystemInfo | test::kFakeVirtualQuery |
                                  test::kFakeVirtualProtect | test::kFakeGetLastError;
    FakeWin32 wa(seed, imports, config);
    FakeWin32 wb(seed, imports, config);
    const Functions originals = Originals();
    const std::uint32_t savedPageSize = g_detachPageSize.get();
    Call ca{Op::Detach, &a, otherwise, nullptr, nullptr, nullptr, nullptr, 0, 0};
    Call cb = ca;
    cb.self = &b;
    std::vector<DebugLogCall> logA;
    std::vector<DebugLogCall> logB;
    g_detachPageSize.get() = 0;
    wa.Install();
    SetDebugLogCapture(&logA);
    const Outcome oa = Run(&originals, &ca);
    SetDebugLogCapture(nullptr);
    wa.Uninstall();
    g_detachPageSize.get() = 0;
    wb.Install();
    SetDebugLogCapture(&logB);
    const Outcome ob = Run(&ours, &cb);
    SetDebugLogCapture(nullptr);
    wb.Uninstall();
    g_detachPageSize.get() = savedPageSize;
    bool same = S3RE_EXPECT(oa.caught == ob.caught);
    same &= S3RE_EXPECT(wa.log_ == wb.log_);
    same &= S3RE_EXPECT(logA == logB);
    msvc6::String_Destroy(&a.folder, nullptr);
    msvc6::String_Destroy(&b.folder, nullptr);
    return same;
}

// --------------------------------------------------------------------------
// Open on the real files
// --------------------------------------------------------------------------

struct OpenState {
    Outcome outcome;
    std::vector<DebugLogCall> logs;
    std::vector<std::uint32_t> dsound;
};

OpenState RunOpen(const Functions& f, CSoundData* sound, std::uint64_t seed, const test::FakeDSoundConfig& config,
                  const String* basePath, std::uint32_t window, std::uint32_t detachMode, std::uint32_t format) {
    OpenState state;
    FakeDSound fake(seed, config);
    FakeDSound::ResetObjects();
    // The lost-buffer path calls g_directSound's slot 1: a logging fake.
    sound->base.vtable = reinterpret_cast<std::uint32_t>(FakeDSound::Game(1)->vtable);
    g_directSound = sound;
    Call call{Op::Open, sound, window, nullptr, nullptr, nullptr, basePath, detachMode, format};
    fake.Install();
    SetDebugLogCapture(&state.logs);
    state.outcome = Run(&f, &call);
    SetDebugLogCapture(nullptr);
    fake.Uninstall();
    state.dsound = fake.log_;
    return state;
}

bool SameOpen(test::Context& ctx, const CSoundData* a, const CSoundData* b, const OpenState& sa,
              const OpenState& sb) {
    bool same = S3RE_EXPECT(sa.outcome.caught == sb.outcome.caught);
    same &= S3RE_EXPECT(sa.logs == sb.logs);
    same &= S3RE_EXPECT(sa.dsound == sb.dsound);
    same &= S3RE_EXPECT(Text(a->folder) == Text(b->folder));
    same &= S3RE_EXPECT_EQ(a->base.sound, b->base.sound);
    same &= S3RE_EXPECT_EQ(a->format, b->format);
    same &= S3RE_EXPECT_EQ(a->enabled, b->enabled);
    same &= S3RE_EXPECT_EQ(a->unk148, b->unk148);
    same &= S3RE_EXPECT_EQ(a->primary != nullptr, b->primary != nullptr);
    if (a->primary != nullptr && b->primary != nullptr) {
        same &= S3RE_EXPECT_MEM_EQ(a->primary, b->primary, sizeof(framework::CDirectSoundBuffer));
    }
    for (std::size_t i = 0; i < sound::kFiles; ++i) {
        same &= S3RE_EXPECT_EQ(a->views[i] != 0, b->views[i] != 0);
        same &= S3RE_EXPECT_EQ(a->fileSizes[i], b->fileSizes[i]);
        if (a->views[i] != 0 && b->views[i] != 0) {
            same &= S3RE_EXPECT_EQ(a->tables[i] - a->views[i], b->tables[i] - b->views[i]);
            same &= S3RE_EXPECT_MEM_EQ(reinterpret_cast<const void*>(a->views[i]),
                                       reinterpret_cast<const void*>(b->views[i]), 0x1c);
        }
    }
    return same;
}

// Returns the number of files the original mapped.
int CompareOpenOnce(test::Context& ctx, const Functions& ours, std::uint32_t format, std::uint32_t detachMode,
                    std::uint32_t failChance, const std::vector<std::int32_t>* failures) {
    auto* a = static_cast<CSoundData*>(msvc6::GameOperatorNew(sizeof(CSoundData)));
    auto* b = static_cast<CSoundData*>(msvc6::GameOperatorNew(sizeof(CSoundData)));
    ctx.rng().Fill(a, sizeof(*a));
    std::memcpy(b, a, sizeof(*a));
    void* const savedSound = g_directSound.get();
    Original_CSoundData_Construct(a, nullptr);
    Original_CSoundData_Construct(b, nullptr);
    String basePath{};
    const std::uint64_t seed = ctx.rng().Next64();
    const std::uint32_t window = ctx.rng().Next32();
    test::FakeDSoundConfig config;
    config.failChance = failChance;
    config.failures = failures;
    const OpenState sa = RunOpen(Originals(), a, seed, config, &basePath, window, detachMode, format);
    const OpenState sb = RunOpen(ours, b, seed, config, &basePath, window, detachMode, format);
    if (!SameOpen(ctx, a, b, sa, sb)) {
        ctx.Fail(__FILE__, __LINE__, "  format %u, detach %u, fail chance %u", format, detachMode, failChance);
    }
    int mapped = 0;
    for (std::size_t i = 0; i < sound::kFiles; ++i) {
        mapped += a->views[i] != 0 ? 1 : 0;
    }
    if (failChance == 0 && (format == 1 || format == 2 || format == 4)) {
        S3RE_EXPECT_EQ(a->enabled, static_cast<std::uint8_t>(1));  // the whole path ran
        S3RE_EXPECT_EQ(a->unk148, 1);                                // from siedler3_01.dat
    }
    // Destroy both (the original destructor, with a quiet DirectSound). A
    // primary buffer whose creation failed has no interface, and the
    // original's ReleaseBuffers would release it unchecked: give it one.
    FakeDSound quiet(1, {0});
    quiet.Install();
    for (CSoundData* s : {a, b}) {
        if (s->primary != nullptr && static_cast<framework::CDirectSoundBuffer*>(s->primary)->buffer == nullptr) {
            static_cast<framework::CDirectSoundBuffer*>(s->primary)->buffer = FakeDSound::Buffer(0);
        }
        s->base.vtable = vtables::kCSoundData;
        g_directSound = s;
        Original_CSoundData_DeletingDestroy(s, nullptr, 1);
    }
    quiet.Uninstall();
    g_directSound = savedSound;
    return mapped;
}

// Failure sets for the scripted DirectSound in Open.
const std::vector<std::int32_t> kOpenFailures[] = {
    {static_cast<std::int32_t>(0x88780078)},  // DSERR_NODRIVER: no sound card
    {static_cast<std::int32_t>(0x8878000a)},  // DSERR_ALLOCATED: the card is busy
    {static_cast<std::int32_t>(0x88780064)},  // DSERR_BADFORMAT
    {static_cast<std::int32_t>(0x80004005), static_cast<std::int32_t>(0x88780096),
     static_cast<std::int32_t>(0x80070057), 1},
};

void CompareOpenReal(test::Context& ctx, const Functions& ours) {
    for (std::uint32_t format = 0; format <= 5; ++format) {
        for (std::uint32_t detachMode : {0u, 2u, 3u}) {
            const int mapped = CompareOpenOnce(ctx, ours, format, detachMode, 0, nullptr);
            if (format == 1 || format == 2 || format == 4) {
                S3RE_EXPECT_EQ(mapped, 2);
            }
        }
    }
    for (int i = 0; i < 60; ++i) {
        static const std::uint32_t kFormats[] = {1, 2, 4};
        const std::uint32_t format = kFormats[ctx.rng().Below(3)];
        const std::uint32_t detachMode = ctx.rng().Below(4);
        CompareOpenOnce(ctx, ours, format, detachMode, 1 + ctx.rng().Below(8), &kOpenFailures[i % 4]);
    }
}

void CompareScripted(test::Context& ctx, const Functions& ours, int cases) {
    for (int i = 0; i < cases; ++i) {
        bool same;
        switch (i % 3) {
            case 0: same = CompareLifetime(ctx, ours); break;
            case 1: same = CompareFind(ctx, ours); break;
            default: same = CompareDetach(ctx, ours); break;
        }
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d", i);
            break;
        }
    }
}

// Wrong: the folder without the trailing backslash.
String* __fastcall WrongFind(void* self, void* edx, String* result, void* folders, const String* file) {
    CSoundData_FindDirectoryContainingFile(self, edx, result, folders, file);
    if (result->len > 0) {
        msvc6::String_Erase(result, nullptr, result->len - 1, 1);
    }
    return result;
}

// Wrong: Close leaves the sound enabled.
void __fastcall WrongClose(void* self, void* edx) {
    const std::uint8_t enabled = static_cast<CSoundData*>(self)->enabled;
    CSoundData_Close(self, edx);
    static_cast<CSoundData*>(self)->enabled = enabled;
}

// Wrong: unk148 from the last file instead of the first.
void __fastcall WrongOpen(void* self, void* edx, std::uint32_t window, const String* basePath,
                          std::uint32_t detachMode, std::uint32_t format) {
    CSoundData_Open(self, edx, window, basePath, detachMode, format);
    CSoundData* const sound = static_cast<CSoundData*>(self);
    if (sound->views[0] != 0) {
        sound->unk148 = reinterpret_cast<const sound::SndFileHeader*>(sound->views[0])->unk14;
    }
}

}  // namespace

S3RE_TEST(SoundData_OpenMatchesOriginal) {
    CompareOpenReal(ctx, Ours());
}

S3RE_TEST(SoundData_MatchOriginal) {
    CompareScripted(ctx, Ours(), 1500);
}

S3RE_TEST(SoundData_ComparisonDetectsWrongImplementation) {
    Functions wrong = Ours();
    wrong.find = &WrongFind;
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareScripted(probe, wrong, 300);
    S3RE_EXPECT(probe.failures() > 0);
    wrong = Ours();
    wrong.close = &WrongClose;
    test::Context probe2(ctx.name(), 2, /*quiet=*/true);
    CompareScripted(probe2, wrong, 600);
    S3RE_EXPECT(probe2.failures() > 0);
    wrong = Ours();
    wrong.open = &WrongOpen;
    test::Context probe3(ctx.name(), 3, /*quiet=*/true);
    CompareOpenReal(probe3, wrong);
    S3RE_EXPECT(probe3.failures() > 0);
}

}  // namespace s3re::game

namespace s3re::game {

// For the emulator's self-test (tools/emutest/specs/self_test.py): the
// constructor with a wrong folder length (+0x18, near the ignored
// allocator byte +0x10).
void* __fastcall WrongSoundConstruct(void* self, void* edx) {
    CSoundData_Construct(self, edx);
    static_cast<sound::CSoundData*>(self)->folder.len = 1;
    return self;
}

// The search without the trailing backslash (self-test of the result's
// ignored allocator byte).
msvc6::String* __fastcall WrongSoundFindDirectory(void* self, void* edx, msvc6::String* result, void* folders,
                                                  const msvc6::String* file) {
    CSoundData_FindDirectoryContainingFile(self, edx, result, folders, file);
    if (result->len > 0) {
        msvc6::String_Erase(result, nullptr, result->len - 1, 1);
    }
    return result;
}

}  // namespace s3re::game
