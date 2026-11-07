// CLogicThread (see "Runtime structure" in CLAUDE.md).
//
// CLogicThread::Step (0x0053db30): one simulation step; vtable slot 9 of the
// thread vtable, called repeatedly by DirectX::CGameThread::Main. thiscall,
// no stack arguments, plain `ret` (EAX is not meaningful to the caller).
//
// Status: pass-through hook, used for the determinism trace. Not
// reimplemented.

#include "hooks.h"
#include "trace/determinism.h"

namespace s3re::game {

struct CLogicThread;  // opaque

namespace {

using StepFn = void(__fastcall*)(CLogicThread* self, void* edx);

void __fastcall Hooked_CLogicThread_Step(CLogicThread* self, void* edx);

S3RE_PASSTHROUGH(0x0053db30, CLogicThread_Step, StepFn, Original_CLogicThread_Step,
                 Hooked_CLogicThread_Step);

void __fastcall Hooked_CLogicThread_Step(CLogicThread* self, void* edx) {
    Original_CLogicThread_Step(self, edx);
    trace::OnLogicStep(self);
}

}  // namespace

}  // namespace s3re::game
