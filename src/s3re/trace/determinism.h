#pragma once

// Determinism trace.
//
// Enabled with `s3re_launcher --trace-determinism` (environment variable
// S3RE_TRACE=determinism). After every simulation step it records the game
// tick and the RNG state, and every kHashInterval ticks it records hashes of
// world memory regions, in determinism-<date>-<time>.tsv next to s3re.dll.
// Compare two traces with `python tools/compare_traces.py a.tsv b.tsv`.

namespace s3re::trace {

bool DeterminismEnabled();

// Called after each original CLogicThread::Step with the CLogicThread object.
void OnLogicStep(const void* logicThread);

void CloseDeterminismTrace();

}  // namespace s3re::trace
