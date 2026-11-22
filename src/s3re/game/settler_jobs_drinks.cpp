// CGameData::UpdateSettlers (0x004e9670), job handlers 0x4f335a..0x4f5f6b of the
// original (settler_jobs.h): jobs 0xd4, 0xd5, the land-taking job 0x12e/0x12f, the rice farmer (0xe2..
// 0xf2), winegrower (0xf3..0x104), distiller, brewer and powder maker
// (0x105..0x122) and the charcoal burner (0x11a..0x127).
//
// Work in progress: handlers not written yet return JobResult::NotImplemented.

#include "game/settler_jobs.h"
#include "game/settler_jobs_test.h"

namespace s3re::game::settler_jobs {

JobResult RunDrinkJobs(SettlerContext& ctx) {
    (void)ctx;
    return JobResult::NotImplemented;
}

}  // namespace s3re::game::settler_jobs
