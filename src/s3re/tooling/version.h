#pragma once

// Version of the DLL code that still runs in original mode
// (`s3re_launcher --original`): every S3RE_PASSTHROUGH and S3RE_HOOK_ALWAYS
// hook and what it calls (determinism trace, command recorder and replay,
// --seed, --load-save, --exit-after-ticks, --fast, DebugLog and crash
// logging...).
//
// tools/trace_cache.py reads this string from s3re.dll and makes it part of
// the key of every cached reference trace of the original code. BUMP THE
// NUMBER whenever such code changes in a way that could change what the
// original game does or what the trace records; otherwise checks keep
// comparing against stale cached references. (The trace format has its own
// version, "s3re determinism trace vN" in trace/determinism.cpp, which is
// part of the key as well.)
#define S3RE_ORIGINAL_TOOLING_VERSION "s3re original-mode tooling version 3"
