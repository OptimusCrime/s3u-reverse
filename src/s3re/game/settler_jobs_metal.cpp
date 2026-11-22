// CGameData::UpdateSettlers (0x004e9670), job handlers 0x4ee0c1..0x4f0816 of the
// original (settler_jobs.h): the sawmill tail (0x31..0x34), smelters (0x38..0x4b), miners (0x52..0x63,
// 0xd6..0xe1, MinerJob*), jobs 0x186..0x18f (NoOp), 0x64, 0x36, 0xaf, the
// tool smith (0x6b, 0x74, 0x7d..0x82) and the war machine workshops
// (0x128..0x139, "Katapultproduktion").
//
// Work in progress: handlers not written yet return JobResult::NotImplemented.

#include "game/settler_jobs.h"
#include "game/settler_jobs_test.h"

namespace s3re::game::settler_jobs {

JobResult RunMetalJobs(SettlerContext& ctx) {
    (void)ctx;
    return JobResult::NotImplemented;
}

}  // namespace s3re::game::settler_jobs
