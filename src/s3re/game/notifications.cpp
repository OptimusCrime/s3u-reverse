// Notifications of the local player (messages such as "a building is under
// attack", with a position the player can jump to).
//
// CGameData::NotifyLocalPlayer (0x004fec10): (player, type, x, y). Only for
//   the local player (world+0x2d2d4). The kind table at S3.EXE 0x659dd0
//   (20 bytes per type: list 1..4, level, icon/graphics id, repeat time,
//   radius) decides: nothing happens if the type's level is below
//   9 - the notification level setting (GetMemberDword64 on the config
//   object 0x7a7af0; signed compare). With a radius, a notification of the
//   same type within that distance (TileDistance < radius) of a running
//   repeat timer (world+0x2e5e0, 10 per type, the list ends at the first
//   free one) is suppressed. Otherwise the notification {type, x, y} goes
//   into the first free entry (type 0) of its list (world+0x2d320, 4 x
//   100; nothing if full) with a sound/UI feedback (PushUiFeedback 0x50
//   for types 3 and 20; 0x50 then 0x5a for types 4 and 10; else 0x6e), and
//   with a radius a repeat timer {repeat time, x, y} starts in the first
//   free timer slot of the type (none if all 10 run; the timer is set even
//   when the list was full). EAX: callers don't use it. ret 0x10.
// thiscall (ECX = world).

#include "game/globals.h"
#include "game/replacements.h"
#include "game/tables.h"
#include "game/world.h"
#include "hooks.h"
#include "test/difftest.h"
#include "test/world_buffer.h"

#include <cstddef>
#include <cstdint>

namespace s3re::game {

std::uint32_t __fastcall GetMemberDword64(void* self, void* edx);

namespace {

using layout::Notification;
using layout::NotificationTimer;


constexpr std::uint32_t kListEntries = 100;
constexpr std::uint32_t kTimersPerType = 10;

}  // namespace

using NotifyFn = std::uint32_t(__fastcall*)(CGameData* world, void* edx, std::uint32_t player, std::uint32_t type,
                                            std::uint32_t x, std::uint32_t y);

// Purpose: Tells the local player about an event at a place (e.g. an attack, a mine running out), unless
// the notification level hides it or the same message came from nearby a short while ago.
std::uint32_t __fastcall CGameData_NotifyLocalPlayer(CGameData* world, void*, std::uint32_t player,
                                                     std::uint32_t type, std::uint32_t x, std::uint32_t y) {
    if (static_cast<std::int32_t>(player) != world->local_player) {
        return 0;
    }
    const NotificationKind& kind = g_notificationKinds[type];
    const auto setting = static_cast<std::int32_t>(GetMemberDword64(&g_config, nullptr));
    if (kind.level < 9 - setting) {
        return 0;
    }
    NotificationTimer* const timers = world->notification_timers[type];
    if (kind.radius != 0) {
        bool recent = false;
        for (std::uint32_t k = 0; k < kTimersPerType && timers[k].countdown != 0; ++k) {
            if (TileDistance(x, y, static_cast<std::uint32_t>(timers[k].x), static_cast<std::uint32_t>(timers[k].y)) <
                kind.radius) {
                recent = true;
            }
        }
        if (recent) {
            return 0;
        }
    }
    // The original indexes with list - 1 in 32 bits (list 0 lies before the array).
    Notification* const list = &world->notifications[0][0] + (kind.list - 1) * static_cast<std::int32_t>(kListEntries);
    for (std::uint32_t k = 0; k < kListEntries; ++k) {
        if (list[k].type != 0) {
            continue;
        }
        list[k].type = static_cast<std::int32_t>(type);
        list[k].x = static_cast<std::int32_t>(x);
        list[k].y = static_cast<std::int32_t>(y);
        switch (type) {
        case 3:
        case 20:
            CGameData_PushUiFeedback(world, nullptr, 0x50);
            break;
        case 4:
        case 10:
            CGameData_PushUiFeedback(world, nullptr, 0x50);
            CGameData_PushUiFeedback(world, nullptr, 0x5a);
            break;
        default:
            CGameData_PushUiFeedback(world, nullptr, 0x6e);
            break;
        }
        break;
    }
    if (kind.radius == 0) {
        return 0;
    }
    for (std::uint32_t k = 0; k < kTimersPerType; ++k) {
        if (timers[k].countdown == 0) {
            timers[k].countdown = kind.repeat;
            timers[k].x = static_cast<std::int32_t>(x);
            timers[k].y = static_cast<std::int32_t>(y);
            break;
        }
    }
    return 0;  // callers don't use EAX
}

S3RE_HOOK(0x004fec10, CGameData_NotifyLocalPlayer, NotifyFn, Original_CGameData_NotifyLocalPlayer,
          CGameData_NotifyLocalPlayer);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kTypes = 42;

// Wrong: the timers are checked with <= radius.
std::uint32_t __fastcall WrongNotifyLocalPlayer(CGameData* world, void* edx, std::uint32_t player, std::uint32_t type,
                                                std::uint32_t x, std::uint32_t y) {
    const NotificationKind& kind = g_notificationKinds[type];
    if (kind.radius != 0 && static_cast<std::int32_t>(player) == world->local_player) {
        for (std::uint32_t k = 0; k < kTimersPerType && world->notification_timers[type][k].countdown != 0; ++k) {
            const NotificationTimer& t = world->notification_timers[type][k];
            if (TileDistance(x, y, t.x, t.y) == kind.radius) {
                return 0;
            }
        }
    }
    return CGameData_NotifyLocalPlayer(world, edx, player, type, x, y);
}

void CompareNotify(test::Context& ctx, NotifyFn candidate, int cases) {
    std::uint32_t& setting = g_config->notification_level;
    const std::uint32_t savedSetting = setting;
    // Full-size worlds: PushUiFeedback writes world+0x1539484.
    test::WorldPair full(ctx, sizeof(CGameData));
    for (int i = 0; i < cases; ++i) {
        const auto local = static_cast<std::int32_t>(ctx.rng().Below(4));
        const std::uint32_t player = ctx.rng().Chance(4, 5) ? static_cast<std::uint32_t>(local) : ctx.rng().Below(4);
        const std::uint32_t type = ctx.rng().Below(kTypes);
        const std::uint32_t x = ctx.rng().Below(384), y = ctx.rng().Below(384);
        setting = ctx.rng().Chance(1, 10) ? ctx.rng().Next32() : ctx.rng().Below(10);
        // Timers of the type: a few running ones near (x, y), then free.
        const std::uint32_t running = ctx.rng().Chance(1, 8) ? kTimersPerType : ctx.rng().Below(4);
        const NotificationKind& kind = g_notificationKinds[type];
        for (std::uint32_t k = 0; k < kTimersPerType; ++k) {
            NotificationTimer t{};
            if (k < running) {
                const std::uint32_t reach = kind.radius + 3;
                t.countdown = static_cast<std::int32_t>(1 + ctx.rng().Below(100));
                t.x = static_cast<std::int32_t>(x + ctx.rng().Below(2 * reach + 1) - reach);
                t.y = static_cast<std::int32_t>(y + ctx.rng().Below(2 * reach + 1) - reach);
            } else if (ctx.rng().Chance(1, 4)) {
                t.countdown = static_cast<std::int32_t>(ctx.rng().Next32());  // after a gap
            }
            full.Both([&](CGameData* w) { w->notification_timers[type][k] = t; });
        }
        // The list: mostly full up to a point.
        if (kind.list >= 1 && kind.list <= 4) {
            const std::uint32_t used = ctx.rng().Chance(1, 8) ? kListEntries : ctx.rng().Below(10);
            for (std::uint32_t k = 0; k < kListEntries; ++k) {
                const auto value = static_cast<std::int32_t>(k < used ? 1 + ctx.rng().Below(40) : ctx.rng().Chance(1, 3) ? 7 : 0);
                full.Both([&](CGameData* w) { w->notifications[kind.list - 1][k].type = value; });
            }
        }
        const std::uint32_t count = ctx.rng().Below(11);
        full.Both([&](CGameData* w) {
            w->local_player = local;
            w->ui_feedback_count = count;
        });
        Original_CGameData_NotifyLocalPlayer(full.original(), nullptr, player, type, x, y);
        candidate(full.ours(), nullptr, player, type, x, y);
        const bool same = full.Same(ctx, offsetof(CGameData, notifications),
                                    offsetof(CGameData, pending_events) - offsetof(CGameData, notifications)) &&
                          full.Same(ctx, offsetof(CGameData, ui_feedback), offsetof(CGameData, flag15394b0) - offsetof(CGameData, ui_feedback));
        if (!same || (i % 512 == 511 && !full.Checkpoint(ctx))) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: player %u (local %d) type %u at (%u, %u), setting %u", i, player,
                     local, type, x, y, setting);
            break;
        }
    }
    setting = savedSetting;
}

}  // namespace

S3RE_TEST(CGameData_NotifyLocalPlayer_MatchesOriginal) {
    CompareNotify(ctx, &CGameData_NotifyLocalPlayer, 20000);
}
S3RE_TEST(CGameData_NotifyLocalPlayer_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareNotify(probe, &WrongNotifyLocalPlayer, 5000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
