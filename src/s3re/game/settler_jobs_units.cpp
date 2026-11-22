// CGameData::UpdateSettlers (0x004e9670), job handlers 0x4e971c..0x4eb43b of the
// original (settler_jobs.h): new settlers (job 0), idle civilians (1) and units (0x37, 0x8e), soldiers
// entering and leaving towers (0xac..0xae), the walking jobs (WalkSettler),
// job 4 (arriving at the workplace: tables 0x4f98c4 by settler type and
// 0x4f9984/0x4f999c by mine type) and job 0x170.
//
// Work in progress: handlers not written yet return JobResult::NotImplemented.

#include "game/settler_jobs.h"
#include "game/settler_jobs_test.h"

namespace s3re::game::settler_jobs {

JobResult RunUnitJobs(SettlerContext& ctx) {
    (void)ctx;
    return JobResult::NotImplemented;
}

}  // namespace s3re::game::settler_jobs
