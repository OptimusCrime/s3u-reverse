// Win32::CCDAudio (Win32\cdaudio.cpp; RTTI name): the music player for
// the CD audio tracks (on the GOG release, mciSendCommandA is GOG's
// _INMM.dll, which plays the tracks from files). Layout and the other
// members: re/win32_framework.md.
//
// CCDAudio::CCDAudio (0x006154c0): +4, +8, +0xc, +0x14 = 0 (+0x10 is left),
//   vtable 0x65d944. EAX = this.
// SetTimeFormatTMSF (0x00615e60): mciSendCommandA(device +8, MCI_SET
//   (0x80d), MCI_SET_TIME_FORMAT (0x400), {callback (uninitialized),
//   MCI_FORMAT_TMSF (10)}); an error is logged ("CD: Error during
//   SetTimeFormat: %u"). EAX: 0, or the error (the release DebugLog is a bare
//   `ret`; Init passes EAX on).
// Play (0x00615ea0): (from, to) ret 8: with a window (+0xc),
//   PostMessageA(window, 0x600, 0, to << 16 | from (16 bits each)); the
//   window procedure plays on the thread that created it. EAX: the window
//   (0) or PostMessage's result.
// Stop (0x00615f70): with a window, PostMessageA(window, 0x601, 0, 0).
// DoStop (0x00615f90): with a device, mciSendCommandA(device, MCI_STOP
//   (0x808), 0, null); an error is logged ("CD: Error during stop: %u").
//   EAX as SetTimeFormatTMSF's (or the null device).
// GetTrackCount (0x00615fc0): with a device, mciSendCommandA(device,
//   MCI_STATUS (0x814), MCI_STATUS_ITEM (0x100), {callback (uninitialized),
//   return, MCI_STATUS_NUMBER_OF_TRACKS (3)}): MCIERR_UNSUPPORTED_FUNCTION
//   (0x106) gives 0 without a log, another error is logged ("CD: Error
//   during GetTrackCount: %u") and gives 0, success the returned count.
//   Without a device EAX = 0 (the device field).
//
// The parameter blocks' callback fields are uninitialized locals in the
// original (MCI_NOTIFY is not set, so they are not read); ours are 0.

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

#pragma pack(push, 1)
struct CCDAudio {
    std::uint32_t vtable;    // +0x00
    void* instance;          // +0x04 HINSTANCE (Init)
    std::uint32_t device;    // +0x08 MCI device id (0: closed)
    void* window;            // +0x0c the hidden callback window
    void* notifyWindow;      // +0x10 MM_MCINOTIFY is forwarded there (Init)
    std::uint32_t notified;  // +0x14 the last MM_MCINOTIFY wParam
};
#pragma pack(pop)
static_assert(offsetof(CCDAudio, notified) == 0x14);

constexpr std::uint32_t kMciSet = 0x80d;
constexpr std::uint32_t kMciStop = 0x808;
constexpr std::uint32_t kMciStatus = 0x814;
constexpr std::uint32_t kMciSetTimeFormat = 0x400;  // MCI_SET_TIME_FORMAT
constexpr std::uint32_t kMciStatusItem = 0x100;     // MCI_STATUS_ITEM
constexpr std::uint32_t kFormatTmsf = 10;            // MCI_FORMAT_TMSF
constexpr std::uint32_t kStatusNumberOfTracks = 3;   // MCI_STATUS_NUMBER_OF_TRACKS
constexpr std::uint32_t kUnsupportedFunction = 0x106;  // MCIERR_UNSUPPORTED_FUNCTION
constexpr std::uint32_t kMessagePlay = 0x600;       // to the CD window: play (lParam: to << 16 | from)
constexpr std::uint32_t kMessageStop = 0x601;

CCDAudio* AsCD(void* self) {
    return static_cast<CCDAudio*>(self);
}

std::uint32_t SendCommand(std::uint32_t device, std::uint32_t message, std::uint32_t flags, void* parameters) {
    return g_importMciSendCommandA.get()(device, message, flags, parameters);
}

// DebugLog (0x428720) with the error. EAX: the error, which the game's
// DebugLog (a bare `ret` in the release build) leaves in EAX after the
// original's `push eax`; SetTimeFormatTMSF's EAX reaches Init's caller.
std::uint32_t LogError(const GameText& format, std::uint32_t error) {
    GameDebugLog(format, error);
    return error;
}

}  // namespace

using CDThisFn = void*(__fastcall*)(void* self, void* edx);
using CDValueFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using CDPlayFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t from, std::uint32_t to);

void* __fastcall Win32_CCDAudio_Construct(void* self, void* edx);
std::uint32_t __fastcall Win32_CCDAudio_SetTimeFormatTMSF(void* self, void* edx);
std::uint32_t __fastcall Win32_CCDAudio_Play(void* self, void* edx, std::uint32_t from, std::uint32_t to);
std::uint32_t __fastcall Win32_CCDAudio_Stop(void* self, void* edx);
std::uint32_t __fastcall Win32_CCDAudio_DoStop(void* self, void* edx);
std::uint32_t __fastcall Win32_CCDAudio_GetTrackCount(void* self, void* edx);

S3RE_HOOK(0x006154c0, Win32_CCDAudio_Construct, CDThisFn, Original_Win32_CCDAudio_Construct, Win32_CCDAudio_Construct);

// Purpose: constructor of the CD music player (not opened yet).
void* __fastcall Win32_CCDAudio_Construct(void* self, void* /*edx*/) {
    CCDAudio* const cd = AsCD(self);
    cd->instance = nullptr;
    cd->device = 0;
    cd->window = nullptr;
    cd->notified = 0;
    cd->vtable = vtables::kCCDAudio;
    return self;
}

S3RE_HOOK(0x00615e60, Win32_CCDAudio_SetTimeFormatTMSF, CDValueFn, Original_Win32_CCDAudio_SetTimeFormatTMSF,
          Win32_CCDAudio_SetTimeFormatTMSF);

// Purpose: makes the device count in tracks (track/minute/second/frame), so
// that Play takes track numbers.
std::uint32_t __fastcall Win32_CCDAudio_SetTimeFormatTMSF(void* self, void* /*edx*/) {
    std::uint32_t parameters[3] = {0, kFormatTmsf, 0};  // MCI_SET_PARMS (callback, time format, audio)
    const std::uint32_t error = SendCommand(AsCD(self)->device, kMciSet, kMciSetTimeFormat, parameters);
    if (error != 0) {
        return LogError(texts::kCDSetTimeFormatError, error);
    }
    return 0;
}

S3RE_HOOK(0x00615ea0, Win32_CCDAudio_Play, CDPlayFn, Original_Win32_CCDAudio_Play, Win32_CCDAudio_Play);

// Purpose: asks the CD window's thread to play tracks `from` to `to`.
std::uint32_t __fastcall Win32_CCDAudio_Play(void* self, void* /*edx*/, std::uint32_t from, std::uint32_t to) {
    void* const window = AsCD(self)->window;
    if (window == nullptr) {
        return 0;
    }
    const std::uint32_t range = (to & 0xFFFF) << 16 | (from & 0xFFFF);
    return static_cast<std::uint32_t>(g_importPostMessageA.get()(window, kMessagePlay, 0, range));
}

S3RE_HOOK(0x00615f70, Win32_CCDAudio_Stop, CDValueFn, Original_Win32_CCDAudio_Stop, Win32_CCDAudio_Stop);

// Purpose: asks the CD window's thread to stop the music.
std::uint32_t __fastcall Win32_CCDAudio_Stop(void* self, void* /*edx*/) {
    void* const window = AsCD(self)->window;
    if (window == nullptr) {
        return 0;
    }
    return static_cast<std::uint32_t>(g_importPostMessageA.get()(window, kMessageStop, 0, 0));
}

S3RE_HOOK(0x00615f90, Win32_CCDAudio_DoStop, CDValueFn, Original_Win32_CCDAudio_DoStop, Win32_CCDAudio_DoStop);

// Purpose: stops the CD device (on the CD window's thread).
std::uint32_t __fastcall Win32_CCDAudio_DoStop(void* self, void* /*edx*/) {
    const std::uint32_t device = AsCD(self)->device;
    if (device == 0) {
        return 0;
    }
    const std::uint32_t error = SendCommand(device, kMciStop, 0, nullptr);
    if (error != 0) {
        return LogError(texts::kCDStopError, error);
    }
    return 0;
}

S3RE_HOOK(0x00615fc0, Win32_CCDAudio_GetTrackCount, CDValueFn, Original_Win32_CCDAudio_GetTrackCount,
          Win32_CCDAudio_GetTrackCount);

// Purpose: the number of tracks on the CD (0 if unknown).
std::uint32_t __fastcall Win32_CCDAudio_GetTrackCount(void* self, void* /*edx*/) {
    const std::uint32_t device = AsCD(self)->device;
    if (device == 0) {
        return 0;
    }
    std::uint32_t parameters[4] = {0, 0, kStatusNumberOfTracks, 0};  // MCI_STATUS_PARMS (callback, return, item, track)
    const std::uint32_t error = SendCommand(device, kMciStatus, kMciStatusItem, parameters);
    if (error == kUnsupportedFunction) {
        return 0;
    }
    if (error != 0) {
        GameDebugLog(texts::kCDGetTrackCountError, error);
        return 0;
    }
    return parameters[1];
}

// ---------------------------------------------------------------------------
// Tests: random objects; mciSendCommandA and PostMessageA replaced (their
// import slots) by fakes that record the call (the parameter block's
// fields after the callback) and return a scripted result (MCI errors
// include 0x106); DebugLog captured. Compared: EAX (the callers' use: the
// track count, Play/Stop's), the object, the calls, the logs.
// ---------------------------------------------------------------------------

namespace {

struct FakeMci {
    test::Rng rng{0};
    std::vector<std::uint32_t> calls;
};
FakeMci* g_mci = nullptr;

std::uint32_t __stdcall FakeMciSendCommand(std::uint32_t device, std::uint32_t message, std::uint32_t flags,
                                           void* parameters) {
    g_mci->calls.insert(g_mci->calls.end(), {0x3c1u, device, message, flags, parameters != nullptr ? 1u : 0u});
    if (parameters != nullptr) {
        // The fields the original sets (time format; status item), not the
        // uninitialized ones.
        const auto* words = static_cast<const std::uint32_t*>(parameters);
        g_mci->calls.push_back(message == kMciStatus ? words[2] : words[1]);
        if (message == kMciStatus) {
            static_cast<std::uint32_t*>(parameters)[1] = g_mci->rng.Below(100);
        }
    }
    static const std::uint32_t kResults[] = {0, 0, 0, kUnsupportedFunction, 0x101, 0x113};
    return g_mci->rng.Chance(1, 8) ? g_mci->rng.Next32() : kResults[g_mci->rng.Below(6)];
}

int __stdcall FakePostMessage(void* window, std::uint32_t message, std::uint32_t wParam, std::uint32_t lParam) {
    g_mci->calls.insert(g_mci->calls.end(), {0x9057u, reinterpret_cast<std::uint32_t>(window), message, wParam, lParam});
    return static_cast<int>(g_mci->rng.Below(2));
}

struct Functions {
    CDThisFn construct;
    CDValueFn setTimeFormat;
    CDPlayFn play;
    CDValueFn stop;
    CDValueFn doStop;
    CDValueFn trackCount;
};

struct Outcome {
    std::uint32_t eax;
    std::vector<std::uint32_t> calls;
    std::vector<DebugLogCall> logs;
    std::uint8_t object[0x20];
};

Outcome RunOne(const Functions& f, std::uint32_t op, const std::uint8_t* initial, std::uint64_t seed,
               std::uint32_t from, std::uint32_t to) {
    Outcome out{};
    std::memcpy(out.object, initial, sizeof(out.object));
    FakeMci mci;
    mci.rng = test::Rng(seed);
    g_mci = &mci;
    const auto send = reinterpret_cast<std::uintptr_t>(&FakeMciSendCommand);
    const auto post = reinterpret_cast<std::uintptr_t>(&FakePostMessage);
    test::PatchBytes(g_importMciSendCommandA.address(), &send, 4);
    test::PatchBytes(g_importPostMessageA.address(), &post, 4);
    SetDebugLogCapture(&out.logs);
    void* const self = out.object + 4;
    switch (op) {
        case 0: out.eax = reinterpret_cast<std::uint32_t>(f.construct(self, nullptr)) == reinterpret_cast<std::uint32_t>(self); break;
        case 1: out.eax = f.setTimeFormat(self, nullptr); break;
        case 2: out.eax = f.play(self, nullptr, from, to); break;
        case 3: out.eax = f.stop(self, nullptr); break;
        case 4: out.eax = f.doStop(self, nullptr); break;
        default: out.eax = f.trackCount(self, nullptr); break;
    }
    SetDebugLogCapture(nullptr);
    test::UnpatchBytes(g_importPostMessageA.address());
    test::UnpatchBytes(g_importMciSendCommandA.address());
    g_mci = nullptr;
    out.calls = mci.calls;
    return out;
}

void Compare(test::Context& ctx, const Functions& ours, int cases) {
    const Functions originals{Original_Win32_CCDAudio_Construct, Original_Win32_CCDAudio_SetTimeFormatTMSF,
                              Original_Win32_CCDAudio_Play,      Original_Win32_CCDAudio_Stop,
                              Original_Win32_CCDAudio_DoStop,    Original_Win32_CCDAudio_GetTrackCount};
    for (int i = 0; i < cases; ++i) {
        std::uint8_t initial[0x20];
        ctx.rng().Fill(initial, sizeof(initial));
        if (ctx.rng().Chance(1, 4)) {
            std::memset(initial + 4 + 8, 0, 4);  // no device
        }
        if (ctx.rng().Chance(1, 4)) {
            std::memset(initial + 4 + 0xc, 0, 4);  // no window
        }
        const std::uint32_t op = ctx.rng().Below(6);
        const std::uint64_t seed = ctx.rng().Next64();
        const std::uint32_t from = ctx.rng().Next32(), to = ctx.rng().Next32();
        const Outcome a = RunOne(originals, op, initial, seed, from, to);
        const Outcome b = RunOne(ours, op, initial, seed, from, to);
        // After a log, the original's EAX is what our DebugLog passthrough
        // leaves (the game's DebugLog is a bare ret: EAX = the error, as ours).
        const bool logged = (op == 1 || op == 4) && !a.logs.empty();
        if (!S3RE_EXPECT_EQ(logged ? 0 : a.eax, logged ? 0 : b.eax) ||
            !S3RE_EXPECT_EQ(logged ? a.logs[0].args[0] : 0, logged ? b.eax : 0) || !S3RE_EXPECT(a.calls == b.calls) || !S3RE_EXPECT(a.logs == b.logs) ||
            !S3RE_EXPECT_MEM_EQ(a.object, b.object, sizeof(a.object))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: op %u", i, op);
            return;
        }
    }
}

// Wrong: an unsupported-function error is logged too.
std::uint32_t __fastcall WrongTrackCount(void* self, void*) {
    const std::uint32_t device = AsCD(self)->device;
    if (device == 0) {
        return 0;
    }
    std::uint32_t parameters[4] = {0, 0, kStatusNumberOfTracks, 0};
    const std::uint32_t error = SendCommand(device, kMciStatus, kMciStatusItem, parameters);
    if (error != 0) {
        GameDebugLog(texts::kCDGetTrackCountError, error);
        return 0;
    }
    return parameters[1];
}

}  // namespace

S3RE_TEST(CDAudio_MatchOriginal) {
    Compare(ctx,
            {&Win32_CCDAudio_Construct, &Win32_CCDAudio_SetTimeFormatTMSF, &Win32_CCDAudio_Play, &Win32_CCDAudio_Stop,
             &Win32_CCDAudio_DoStop, &Win32_CCDAudio_GetTrackCount},
            5000);
}

S3RE_TEST(CDAudio_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe,
            {&Win32_CCDAudio_Construct, &Win32_CCDAudio_SetTimeFormatTMSF, &Win32_CCDAudio_Play, &Win32_CCDAudio_Stop,
             &Win32_CCDAudio_DoStop, &WrongTrackCount},
            2000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
