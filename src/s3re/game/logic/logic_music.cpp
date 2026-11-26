// The CD music of a running game (Volker\LogicInit.cpp; fields in
// logic_thread_layout.h, re/logic_thread.md "Music").
//
// CLogicThread::StartMusic (0x0053f580): (playlist) thiscall, ret 4; from
//   OnStart with the local player's race (or -1). If music is enabled
//   (GameConfig::Byte5E 0x412810 on g_config): +0x65c = +0x660 = playlist,
//   +0x654 = 0, +0x658 = -1, then PlayNextMusicTrack. EAX not used.
// CLogicThread::StopMusic (0x0053f5c0): () thiscall, from OnExit: if music
//   is enabled, +0x654 = 1 and Win32::CCDAudio::Stop on g_cdAudio. EAX not
//   used.
// CLogicThread::PlayNextMusicTrack (0x0053f5f0): () thiscall, from
//   StartMusic and ProcessEvents (a track has ended). The same code four
//   times, once per CD version (g_installedCd: 5, 4, 3, other) with its own
//   track table, playlist stride (4 for the Amazons' CD, else 3), list
//   lengths and DebugLog text. With music enabled and not stopped: a
//   playlist of -1 becomes 0; the track index (signed) moves on while it is
//   >= 0 and below the list's length - 1; otherwise it restarts at 0 and,
//   when the playlist isn't fixed (+0x65c == -1), the next playlist (3 wraps
//   to 0) plays. Then DebugLog(text, track) and CCDAudio::Play(track,
//   track). The playlist is not range-checked: a fixed playlist above 3
//   reads past the tables, as in the original. EAX not used.

#include "game/debug_log.h"
#include "game/globals.h"
#include "game/logic/logic_thread_layout.h"
#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

constexpr std::uintptr_t kMusicEnabled = 0x00412810;  // GameConfig::Byte5E (g_config +0x5e)
constexpr std::uintptr_t kCdPlay = 0x00615ea0;        // Win32::CCDAudio::Play(from, to)
constexpr std::uintptr_t kCdStop = 0x00615f70;        // Win32::CCDAudio::Stop()
constexpr std::uintptr_t kPlayNextMusicTrack = 0x0053f5f0;

constexpr std::int32_t kCyclePlaylists = -1;  // music_fixed_playlist: no fixed list
constexpr std::int32_t kNoPlaylist = -1;
constexpr std::int32_t kNoTrack = -1;
constexpr std::int32_t kPlaylists = 4;

// g_installedCd values.
constexpr std::int32_t kMissionCd = 3;
constexpr std::int32_t kGoldCd1 = 4;
constexpr std::int32_t kGoldCd2 = 5;

// One CD version's music: its track table, tracks per playlist, row of
// g_musicListLengths and log text (addresses: a constant table, no static
// initialization).
struct MusicCd {
    std::uintptr_t tracks;
    std::uint32_t stride;
    std::uint32_t lengths_row;
    std::uintptr_t log_text;
};

constexpr MusicCd kOriginalCd{g_musicTracksOriginal.address(), 3, 0, texts::kPlayOriginalTrack.address()};
constexpr MusicCd kAmazonsCd{g_musicTracksAmazons.address(), 4, 1, texts::kPlayAmazonTrack.address()};
constexpr MusicCd kGold1Cd{g_musicTracksGold1.address(), 3, 2, texts::kPlayGold1Track.address()};
constexpr MusicCd kGold2Cd{g_musicTracksGold2.address(), 3, 3, texts::kPlayGold2Track.address()};

const MusicCd& MusicCdFor(std::int32_t installed) {
    switch (installed) {
        case kGoldCd2: return kGold2Cd;
        case kGoldCd1: return kGold1Cd;
        case kMissionCd: return kAmazonsCd;
        default: return kOriginalCd;
    }
}

// table[index] by 32-bit address arithmetic (no range check, as in the original).
std::int32_t ReadAt(std::uintptr_t table, std::uint32_t index) {
    return *reinterpret_cast<const std::int32_t*>(static_cast<std::uint32_t>(table) + 4 * index);
}

bool MusicEnabled() {
    using Fn = std::uint32_t(__fastcall*)(const void*, void*);
    return (reinterpret_cast<Fn>(kMusicEnabled)(&g_config.get(), nullptr) & 0xff) != 0;
}

void CdPlay(std::int32_t from, std::int32_t to) {
    using Fn = std::uint32_t(__fastcall*)(void*, void*, std::int32_t, std::int32_t);
    reinterpret_cast<Fn>(kCdPlay)(&g_cdAudio.get(), nullptr, from, to);
}

void CdStop() {
    using Fn = std::uint32_t(__fastcall*)(void*, void*);
    reinterpret_cast<Fn>(kCdStop)(&g_cdAudio.get(), nullptr);
}

void CallPlayNextMusicTrack(CLogicThread* thread) {
    using Fn = void(__fastcall*)(CLogicThread*, void*);
    reinterpret_cast<Fn>(kPlayNextMusicTrack)(thread, nullptr);
}

}  // namespace

using MusicFn = void(__fastcall*)(CLogicThread* self, void* edx);
using StartMusicFn = void(__fastcall*)(CLogicThread* self, void* edx, std::int32_t playlist);

// Purpose: Plays the next track of the CD music: the next one of the
// current race's playlist, or the first of the next playlist when it has
// ended (called when a track has finished).
void __fastcall CLogicThread_PlayNextMusicTrack(CLogicThread* self, void*) {
    const MusicCd& cd = MusicCdFor(g_installedCd);
    if (!MusicEnabled() || self->music_stopped != 0) {
        return;
    }
    if (self->music_playlist == kNoPlaylist) {
        self->music_playlist = 0;
    }
    const std::int32_t track = self->music_track;
    // (length - 1 in 32-bit arithmetic, compared signed)
    const std::int32_t last = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(ReadAt(g_musicListLengths.address(), cd.lengths_row * kPlaylists +
                                                                   static_cast<std::uint32_t>(self->music_playlist))) - 1);
    if (track >= 0 && track < last) {
        self->music_track = track + 1;
    } else {
        self->music_track = 0;
        if (self->music_fixed_playlist == kCyclePlaylists) {
            self->music_playlist = self->music_playlist + 1;
            if (self->music_playlist == kPlaylists) {
                self->music_playlist = 0;
            }
        }
    }
    const std::int32_t number = ReadAt(cd.tracks, static_cast<std::uint32_t>(self->music_playlist) * cd.stride +
                                                       static_cast<std::uint32_t>(self->music_track));
    GameDebugLog(GameText(cd.log_text), number);
    CdPlay(number, number);
}

// Purpose: Starts the CD music when a game begins, with the playlist of the
// local player's race (or all playlists in turn for -1).
void __fastcall CLogicThread_StartMusic(CLogicThread* self, void*, std::int32_t playlist) {
    if (!MusicEnabled()) {
        return;
    }
    self->music_fixed_playlist = playlist;
    self->music_playlist = playlist;
    self->music_stopped = 0;
    self->music_track = kNoTrack;
    CallPlayNextMusicTrack(self);
}

// Purpose: Stops the CD music when the game ends.
void __fastcall CLogicThread_StopMusic(CLogicThread* self, void*) {
    if (!MusicEnabled()) {
        return;
    }
    self->music_stopped = 1;
    CdStop();
}

S3RE_HOOK(0x0053f5f0, CLogicThread_PlayNextMusicTrack, MusicFn, Original_CLogicThread_PlayNextMusicTrack,
          CLogicThread_PlayNextMusicTrack);
S3RE_HOOK(0x0053f580, CLogicThread_StartMusic, StartMusicFn, Original_CLogicThread_StartMusic,
          CLogicThread_StartMusic);
S3RE_HOOK(0x0053f5c0, CLogicThread_StopMusic, MusicFn, Original_CLogicThread_StopMusic, CLogicThread_StopMusic);

// ---------------------------------------------------------------------------
// Tests: random thread images with small music fields, the installed CD and
// the music switch (g_config +0x5e) random; CCDAudio Play/Stop are recording
// stubs, DebugLog is captured. Compared: the thread, the calls, the logs.
// StartMusic's call of PlayNextMusicTrack runs whichever is installed at
// 0x53f5f0 (ours when hooked), the same for both sides.
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kThreadBytes = sizeof(CLogicThread);
constexpr std::size_t kMusicSwitch = 0x5e - 0x58;  // GameConfig::bytes58 index of +0x5e

struct MusicCase {
    alignas(4) std::uint8_t thread[kThreadBytes];
    std::int32_t installed;
    std::uint8_t enabled;
    std::int32_t argument;
};

MusicCase RandomCase(test::Rng& rng) {
    MusicCase c{};
    rng.Fill(c.thread, sizeof(c.thread));
    CLogicThread* t = reinterpret_cast<CLogicThread*>(c.thread);
    t->music_stopped = static_cast<std::uint8_t>(rng.Chance(1, 4) ? rng.Below(256) : 0);
    t->music_track = rng.Range(-2, 5);
    // A fixed playlist or the playlist itself stays small: the original reads
    // the tables at any index (a large one would leave S3.EXE's image).
    t->music_fixed_playlist = rng.Chance(1, 2) ? kCyclePlaylists : rng.Range(-2, 5);
    t->music_playlist = rng.Range(-1, 4);
    c.installed = rng.Chance(1, 10) ? static_cast<std::int32_t>(rng.Next32()) : rng.Range(0, 7);
    c.enabled = static_cast<std::uint8_t>(rng.Chance(1, 5) ? 0 : (rng.Chance(1, 4) ? rng.Below(256) : 1));
    c.argument = rng.Chance(1, 3) ? -1 : rng.Range(-2, 5);  // (small, see above)
    return c;
}

struct Outcome {
    std::vector<std::uint8_t> thread;
    std::vector<test::StubCall> calls;
    std::vector<DebugLogCall> logs;
};

Outcome Run(const MusicCase& c, int which, MusicFn next, StartMusicFn start, MusicFn stop) {
    Outcome out;
    out.thread.assign(c.thread, c.thread + kThreadBytes);
    CLogicThread* t = reinterpret_cast<CLogicThread*>(out.thread.data());
    g_installedCd = c.installed;
    g_config->bytes58[kMusicSwitch] = c.enabled;
    test::CalleeStubs stubs;
    stubs.Install<2, kCdPlay>();
    stubs.Install<0, kCdStop>();
    DebugLogRecorder logs;
    switch (which) {
        case 0: next(t, nullptr); break;
        case 1: start(t, nullptr, c.argument); break;
        default: stop(t, nullptr); break;
    }
    out.calls = stubs.Take();
    out.logs = logs.Take();
    return out;
}

void CompareMusic(test::Context& ctx, MusicFn next, StartMusicFn start, MusicFn stop, int cases) {
    const std::int32_t savedCd = g_installedCd;
    const std::uint8_t savedSwitch = g_config->bytes58[kMusicSwitch];
    for (int i = 0; i < cases; ++i) {
        const MusicCase c = RandomCase(ctx.rng());
        const int which = static_cast<int>(ctx.rng().Below(3));
        const Outcome expected = Run(c, which, Original_CLogicThread_PlayNextMusicTrack,
                                     Original_CLogicThread_StartMusic, Original_CLogicThread_StopMusic);
        const Outcome actual = Run(c, which, next, start, stop);
        bool same = S3RE_EXPECT_MEM_EQ(expected.thread.data(), actual.thread.data(), kThreadBytes);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        same &= S3RE_EXPECT(expected.logs == actual.logs);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: function %d, cd %d, enabled %u", i, which, c.installed,
                     c.enabled);
            break;
        }
    }
    g_installedCd = savedCd;
    g_config->bytes58[kMusicSwitch] = savedSwitch;
}

// Wrong: the playlists wrap after 3 instead of 4.
void __fastcall WrongPlayNextMusicTrack(CLogicThread* self, void* edx) {
    const std::int32_t before = self->music_playlist;
    CLogicThread_PlayNextMusicTrack(self, edx);
    if (before == 2 && self->music_playlist == 3 && self->music_track == 0) {
        self->music_playlist = 0;
    }
}

}  // namespace

S3RE_TEST(CLogicThread_Music_MatchesOriginal) {
    CompareMusic(ctx, &CLogicThread_PlayNextMusicTrack, &CLogicThread_StartMusic, &CLogicThread_StopMusic, 6000);
}

S3RE_TEST(CLogicThread_Music_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMusic(probe, &WrongPlayNextMusicTrack, &CLogicThread_StartMusic, &CLogicThread_StopMusic, 3000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
