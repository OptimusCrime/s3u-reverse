// CGameData::UpdateSettlers (0x004e9670), job handlers 0x4eb43b..0x4ee0c1 of the
// original (settler_jobs.h): diggers (5, 7, 8, 9; "Digger trouble"), builders (0xe, 0x12), carriers
// (0x8a, 0x69, 0xa9, 0xaa, 0x4d..0x51, 0xb, 0xd), soldiers at their
// building (0x10, 0x6a, 0x8b, 0x8c), the woodcutter, stonecutter, sawmill
// and forester chains (0x13..0x2f; table 0x4f99d0 for job 0x21).
//
// Work in progress: handlers not written yet return JobResult::NotImplemented.

#include "game/settler_jobs.h"
#include "game/settler_jobs_test.h"

namespace s3re::game::settler_jobs {

JobResult RunBuildingJobs(SettlerContext& ctx) {
    (void)ctx;
    return JobResult::NotImplemented;
}

}  // namespace s3re::game::settler_jobs
