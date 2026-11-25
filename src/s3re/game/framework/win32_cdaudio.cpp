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
// Session 58 (wip/framework-errors), with ThrowMCIError done:
// Open (0x00615740): mciSendCommandA(0, MCI_OPEN (0x803), MCI_OPEN_TYPE |
//   MCI_OPEN_SHAREABLE (0x2100), {callback = the window (+0xc), device 0,
//   type "cdaudio", no element, no alias}); an error (its low word) goes to
//   Win32::ThrowMCIError("error check", cdaudio.cpp, 83, error); then +8 =
//   the device id from the block. EAX: the device id.
// Close (0x00615e20): mciSendCommandA(device, MCI_CLOSE (0x804), 0, null);
//   an error (low word) to ThrowMCIError(..., 155, error); +8 = 0. EAX:
//   mciSendCommandA's result.
// DoPlay (0x00615ee0): (from, to) ret 8, on the CD window's thread: with a
//   device, mciSendCommandA(device, MCI_PLAY (0x806), flags, {callback =
//   the window, from = from & 0xff, to = (to + 1) & 0xff}): MCI_NOTIFY with
//   a window, MCI_FROM unless from is -1, MCI_TO when to is not -1 and less
//   than GetTrackCount() (so the last track plays to the disc's end); an
//   error is logged ("CD: Error during play: %u"). EAX: 0, the error, or
//   the null device.
// ~CCDAudio (0x00615500): vtable 0x65d944; Close when a device is open
//   (EAX: Close's, or the null device).
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

#include "msvc6/catch_match.h"

#include <windows.h>

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
constexpr std::uint32_t kMciOpen = 0x803;
constexpr std::uint32_t kMciClose = 0x804;
constexpr std::uint32_t kMciPlay = 0x806;
constexpr std::uint32_t kOpenTypeShareable = 0x2100;  // MCI_OPEN_TYPE | MCI_OPEN_SHAREABLE
constexpr std::uint32_t kMciNotify = 1;               // MCI_NOTIFY
constexpr std::uint32_t kMciFrom = 4;                 // MCI_FROM
constexpr std::uint32_t kMciTo = 8;                   // MCI_TO
constexpr std::uint32_t kMciErrorMask = 0xFFFF;       // the error code part of an MCIERROR
constexpr std::uint32_t kNoTrack = 0xFFFFFFFF;        // DoPlay's "from the start" / "to the end"
constexpr std::uint32_t kTrackMask = 0xFF;            // MCI_MAKE_TMSF(track, 0, 0, 0)
constexpr std::uint32_t kLineOpen = 83;               // the checks' lines in cdaudio.cpp
constexpr std::uint32_t kLineClose = 155;
constexpr std::uintptr_t kThrowMCIError = 0x006114f0;
constexpr std::uintptr_t kGetTrackCount = 0x00615fc0;
constexpr std::uintptr_t kClose = 0x00615e20;

using ThrowMCIErrorFn = void(__fastcall*)(const char* expression, const char* file, std::uint32_t line,
                                          std::uint32_t error);
using CDCountFn = std::uint32_t(__fastcall*)(void* self, void* edx);

// The MCI checks: an error (its low word) is thrown as CMCIException.
void CheckMci(std::uint32_t error, std::uint32_t line) {
    if ((error & kMciErrorMask) != 0) {
        reinterpret_cast<ThrowMCIErrorFn>(kThrowMCIError)(texts::kErrorCheck.c_str(), texts::kCDAudioFile.c_str(),
                                                          line, error);
    }
}

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
std::uint32_t __fastcall Win32_CCDAudio_Open(void* self, void* edx);
std::uint32_t __fastcall Win32_CCDAudio_Close(void* self, void* edx);
std::uint32_t __fastcall Win32_CCDAudio_DoPlay(void* self, void* edx, std::uint32_t from, std::uint32_t to);
std::uint32_t __fastcall Win32_CCDAudio_Destroy(void* self, void* edx);

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

S3RE_HOOK(0x00615740, Win32_CCDAudio_Open, CDValueFn, Original_Win32_CCDAudio_Open, Win32_CCDAudio_Open);

// Purpose: opens the CD audio device (on GOG: _INMM.dll's track files);
// MCI notifications go to the hidden callback window.
std::uint32_t __fastcall Win32_CCDAudio_Open(void* self, void* /*edx*/) {
    CCDAudio* const cd = AsCD(self);
    // MCI_OPEN_PARMS: callback, device id, device type, element, alias.
    std::uint32_t parameters[5] = {reinterpret_cast<std::uint32_t>(cd->window), 0,
                                   reinterpret_cast<std::uint32_t>(texts::kCDAudioDevice.c_str()), 0, 0};
    const std::uint32_t error = SendCommand(0, kMciOpen, kOpenTypeShareable, parameters);
    CheckMci(error, kLineOpen);
    cd->device = parameters[1];
    return parameters[1];
}

S3RE_HOOK(0x00615e20, Win32_CCDAudio_Close, CDValueFn, Original_Win32_CCDAudio_Close, Win32_CCDAudio_Close);

// Purpose: closes the CD audio device.
std::uint32_t __fastcall Win32_CCDAudio_Close(void* self, void* /*edx*/) {
    const std::uint32_t result = SendCommand(AsCD(self)->device, kMciClose, 0, nullptr);
    CheckMci(result, kLineClose);
    AsCD(self)->device = 0;
    return result;
}

S3RE_HOOK(0x00615ee0, Win32_CCDAudio_DoPlay, CDPlayFn, Original_Win32_CCDAudio_DoPlay, Win32_CCDAudio_DoPlay);

// Purpose: plays tracks `from` to `to` (on the CD window's thread, posted by
// Play); the window is told when the music ends.
std::uint32_t __fastcall Win32_CCDAudio_DoPlay(void* self, void* /*edx*/, std::uint32_t from, std::uint32_t to) {
    CCDAudio* const cd = AsCD(self);
    if (cd->device == 0) {
        return 0;
    }
    // MCI_PLAY_PARMS: callback, from, to.
    std::uint32_t parameters[3] = {reinterpret_cast<std::uint32_t>(cd->window), from & kTrackMask,
                                   (to + 1) & kTrackMask};
    std::uint32_t flags = cd->window != nullptr ? kMciNotify : 0;
    if (from != kNoTrack) {
        flags |= kMciFrom;
    }
    if (to != kNoTrack && to < reinterpret_cast<CDCountFn>(kGetTrackCount)(self, nullptr)) {
        flags |= kMciTo;
    }
    const std::uint32_t error = SendCommand(cd->device, kMciPlay, flags, parameters);
    if (error != 0) {
        return LogError(texts::kCDPlayError, error);
    }
    return 0;
}

S3RE_HOOK(0x00615500, Win32_CCDAudio_Destroy, CDValueFn, Original_Win32_CCDAudio_Destroy, Win32_CCDAudio_Destroy);

// Purpose: destructor of the CD music player (closes the device).
std::uint32_t __fastcall Win32_CCDAudio_Destroy(void* self, void* /*edx*/) {
    CCDAudio* const cd = AsCD(self);
    cd->vtable = vtables::kCCDAudio;
    if (cd->device == 0) {
        return 0;
    }
    return reinterpret_cast<CDCountFn>(kClose)(self, nullptr);
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
        // The fields the original sets (time format; status item; the open
        // and play blocks), not the uninitialized ones.
        auto* words = static_cast<std::uint32_t*>(parameters);
        if (message == kMciOpen) {
            g_mci->calls.insert(g_mci->calls.end(), {words[0], words[1], words[2], words[3], words[4]});
            words[1] = g_mci->rng.Chance(1, 4) ? g_mci->rng.Next32() : g_mci->rng.Below(8);  // the device id
        } else if (message == kMciPlay) {
            g_mci->calls.insert(g_mci->calls.end(), {words[0], words[1], words[2]});
        } else {
            g_mci->calls.push_back(message == kMciStatus ? words[2] : words[1]);
        }
        if (message == kMciStatus) {
            words[1] = g_mci->rng.Below(100);
        }
    }
    static const std::uint32_t kResults[] = {0, 0, 0, kUnsupportedFunction, 0x101, 0x113, 0x10000};
    return g_mci->rng.Chance(1, 8) ? g_mci->rng.Next32() : kResults[g_mci->rng.Below(7)];
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
    CDValueFn open;
    CDValueFn close;
    CDPlayFn doPlay;
    CDValueFn destroy;
};

// A thrown game exception (ThrowMCIError): its vtable, code (+0x18) and
// message text.
struct Thrown {
    bool threw = false;
    std::uint32_t vtable = 0;
    std::uint32_t code = 0;
    std::vector<char> message;
    bool operator==(const Thrown& o) const {
        return threw == o.threw && vtable == o.vtable && code == o.code && message == o.message;
    }
};

int ThrowFilter(const EXCEPTION_POINTERS* info, Thrown* out) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    if (record->ExceptionCode != 0xE06D7363 || record->NumberParameters < 3) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    out->threw = true;
    void* const object = *reinterpret_cast<void* const*>(record->ExceptionInformation[1]);
    if (object != nullptr) {
        const auto* bytes = static_cast<const std::uint8_t*>(object);
        std::memcpy(&out->vtable, bytes, 4);
        std::memcpy(&out->code, bytes + 0x18, 4);
        const char* text = nullptr;
        std::uint32_t length = 0;
        std::memcpy(&text, bytes + 0xc, 4);
        std::memcpy(&length, bytes + 0x10, 4);
        if (text != nullptr) {
            out->message.assign(text, text + length);
        }
    }
    msvc6::DeleteCaughtException(object);
    return EXCEPTION_EXECUTE_HANDLER;
}

std::uint32_t CallOp(const Functions& f, std::uint32_t op, void* self, std::uint32_t from, std::uint32_t to) {
    switch (op) {
        case 0: return reinterpret_cast<std::uint32_t>(f.construct(self, nullptr)) == reinterpret_cast<std::uint32_t>(self);
        case 1: return f.setTimeFormat(self, nullptr);
        case 2: return f.play(self, nullptr, from, to);
        case 3: return f.stop(self, nullptr);
        case 4: return f.doStop(self, nullptr);
        case 5: return f.trackCount(self, nullptr);
        case 6: return f.open(self, nullptr);
        case 7: return f.close(self, nullptr);
        case 8: return f.doPlay(self, nullptr, from, to);
        default: return f.destroy(self, nullptr);
    }
}

// The call under SEH (no objects to unwind here).
std::uint32_t Guarded(const Functions& f, std::uint32_t op, void* self, std::uint32_t from, std::uint32_t to,
                      Thrown* thrown) {
    std::uint32_t eax = 0;
    __try {
        eax = CallOp(f, op, self, from, to);
    } __except (ThrowFilter(GetExceptionInformation(), thrown)) {
    }
    return eax;
}

struct Outcome {
    std::uint32_t eax;
    std::vector<std::uint32_t> calls;
    std::vector<DebugLogCall> logs;
    std::uint8_t object[0x20];
    Thrown thrown;
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
    out.eax = Guarded(f, op, self, from, to, &out.thrown);
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
                              Original_Win32_CCDAudio_DoStop,    Original_Win32_CCDAudio_GetTrackCount,
                              Original_Win32_CCDAudio_Open,      Original_Win32_CCDAudio_Close,
                              Original_Win32_CCDAudio_DoPlay,    Original_Win32_CCDAudio_Destroy};
    for (int i = 0; i < cases; ++i) {
        std::uint8_t initial[0x20];
        ctx.rng().Fill(initial, sizeof(initial));
        if (ctx.rng().Chance(1, 4)) {
            std::memset(initial + 4 + 8, 0, 4);  // no device
        }
        if (ctx.rng().Chance(1, 4)) {
            std::memset(initial + 4 + 0xc, 0, 4);  // no window
        }
        const std::uint32_t op = ctx.rng().Below(10);
        const std::uint64_t seed = ctx.rng().Next64();
        // Track numbers (DoPlay compares `to` with the track count), -1, or anything.
        auto track = [&]() {
            return ctx.rng().Chance(1, 4) ? 0xFFFFFFFFu : ctx.rng().Chance(1, 2) ? ctx.rng().Below(110) : ctx.rng().Next32();
        };
        const std::uint32_t from = track(), to = track();
        const Outcome a = RunOne(originals, op, initial, seed, from, to);
        const Outcome b = RunOne(ours, op, initial, seed, from, to);
        // After a log, the original's EAX is what our DebugLog passthrough
        // leaves (the game's DebugLog is a bare ret: EAX = the error, as ours).
        const bool logged = (op == 1 || op == 4 || op == 8) && !a.logs.empty() &&
                            (op != 8 || a.logs.back().format == texts::kCDPlayError.address());
        // A throw leaves EAX undefined.
        const bool threw = a.thrown.threw || b.thrown.threw;
        if (!S3RE_EXPECT(a.thrown == b.thrown) || !S3RE_EXPECT_EQ(logged || threw ? 0 : a.eax, logged || threw ? 0 : b.eax) ||
            !S3RE_EXPECT_EQ(logged ? a.logs.back().args[0] : 0, logged ? b.eax : 0) || !S3RE_EXPECT(a.calls == b.calls) || !S3RE_EXPECT(a.logs == b.logs) ||
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

// Wrong: the last track gets MCI_TO too.
std::uint32_t __fastcall WrongDoPlay(void* self, void* edx, std::uint32_t from, std::uint32_t to) {
    return Win32_CCDAudio_DoPlay(self, edx, from, to == 0xFFFFFFFF ? to : to + 1);
}

}  // namespace

S3RE_TEST(CDAudio_MatchOriginal) {
    Compare(ctx,
            {&Win32_CCDAudio_Construct, &Win32_CCDAudio_SetTimeFormatTMSF, &Win32_CCDAudio_Play, &Win32_CCDAudio_Stop,
             &Win32_CCDAudio_DoStop, &Win32_CCDAudio_GetTrackCount, &Win32_CCDAudio_Open, &Win32_CCDAudio_Close,
             &Win32_CCDAudio_DoPlay, &Win32_CCDAudio_Destroy},
            10000);
}

S3RE_TEST(CDAudio_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Compare(probe,
            {&Win32_CCDAudio_Construct, &Win32_CCDAudio_SetTimeFormatTMSF, &Win32_CCDAudio_Play, &Win32_CCDAudio_Stop,
             &Win32_CCDAudio_DoStop, &WrongTrackCount, &Win32_CCDAudio_Open, &Win32_CCDAudio_Close,
             &Win32_CCDAudio_DoPlay, &Win32_CCDAudio_Destroy},
            2000);
    S3RE_EXPECT(probe.failures() > 0);
    test::Context probe2(ctx.name(), 2, /*quiet=*/true);
    Compare(probe2,
            {&Win32_CCDAudio_Construct, &Win32_CCDAudio_SetTimeFormatTMSF, &Win32_CCDAudio_Play, &Win32_CCDAudio_Stop,
             &Win32_CCDAudio_DoStop, &Win32_CCDAudio_GetTrackCount, &Win32_CCDAudio_Open, &Win32_CCDAudio_Close,
             &WrongDoPlay, &Win32_CCDAudio_Destroy},
            2000);
    S3RE_EXPECT(probe2.failures() > 0);
}

}  // namespace s3re::game
