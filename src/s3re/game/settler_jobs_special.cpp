// CGameData::UpdateSettlers (0x004e9670), job handlers 0x4f5f6b..0x4f91fb of the
// original (settler_jobs.h): jobs 0x130..0x14f (shipyard, ships: "Schiff laedt ab"), 0x150..0x152
// (healer), the thief (0x193, 0x194), 0x161..0x171, 0x153..0x160, 0x17c..
// 0x185 (incl. the shared handler 0x4f8b7c of 0x17, 0x8d, 0x145, 0x146) and
// the gong production 0x190..0x192.
//
// Work in progress: handlers not written yet return JobResult::NotImplemented.

#include "game/settler_jobs.h"
#include "game/settler_jobs_test.h"

namespace s3re::game::settler_jobs {

JobResult RunSpecialJobs(SettlerContext& ctx) {
    (void)ctx;
    return JobResult::NotImplemented;
}

}  // namespace s3re::game::settler_jobs
