// CGameData::UpdateSettlers (0x004e9670), job handlers 0x4f0816..0x4f335a of the
// original (settler_jobs.h): the weapon smith (0x6c, 0x75, 0x83..0x88), the pig farm (0x6e, 0x77,
// 0xc8..0xd3, 0x131, 0x132), butcher (0x6f, 0x78, 0xc3..0xc7), farmer (0x70,
// 0x79, 0x8f..0x9d), miller (0x71, 0x7a, 0x9e..0xa2), baker (0x72, 0x7b,
// 0xa3..0xa8), fisher (0x6d, 0x76, 0xb1..0xb9), waterworker (0x73, 0x7c,
// 0xba..0xc2).
//
// Work in progress: handlers not written yet return JobResult::NotImplemented.

#include "game/settler_jobs.h"
#include "game/settler_jobs_test.h"

namespace s3re::game::settler_jobs {

JobResult RunFoodJobs(SettlerContext& ctx) {
    (void)ctx;
    return JobResult::NotImplemented;
}

}  // namespace s3re::game::settler_jobs
