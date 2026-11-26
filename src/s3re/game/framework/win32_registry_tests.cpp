// Differential tests of win32_registry.cpp (wip/app-r4). Each case runs the
// original (Original_* trampoline) and ours on the same random memory: one
// static buffer (guard bytes, the key object, a second key object, a
// std::string, texts and out-parameter slots) is filled the same way before
// each run, so pointer arguments are the same numbers for both. Every
// registry function they call is replaced (its import slot) by a fake that
// logs the call and returns a result drawn from an Rng seeded the same for
// both runs (success mostly; ERROR_FILE_NOT_FOUND, ERROR_ACCESS_DENIED,
// ERROR_MORE_DATA, ERROR_NO_MORE_ITEMS or any code otherwise); on success
// the fakes write their out-parameters (a new HKEY, counts, names, data).
// The FILETIME of RegEnumKeyExA is written on every call, so that the
// original's uninitialized local (after ERROR_NO_MORE_ITEMS) is
// deterministic. A thrown game exception is caught with SEH: its type,
// throw info, code and message text are compared, then it is deleted. Also
// compared: EAX, the whole buffer, the fake calls.

#include "game/globals.h"
#include "game/vtables.h"
#include "msvc6/catch_match.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using MemberFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using MemberArgFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a);
using Member2Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b);
using Member3Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c);
using Member4Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c, std::uint32_t d);
using Member6Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c, std::uint32_t d, std::uint32_t e, std::uint32_t f);
using Member7Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a, std::uint32_t b,
                                             std::uint32_t c, std::uint32_t d, std::uint32_t e, std::uint32_t f,
                                             std::uint32_t g);
using Member10Fn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                              std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6,
                                              std::uint32_t a7, std::uint32_t a8, std::uint32_t a9,
                                              std::uint32_t a10);

std::uint32_t __fastcall Win32_CRegistryKey_Create(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t,
                                                   std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_DeleteSubKey(void*, void*, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_Open(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_Close(void*, void*);
std::uint32_t __fastcall Win32_CRegistryKey_QueryInfo(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t,
                                                      std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                                      std::uint32_t, std::uint32_t, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_EnumKey(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t,
                                                    std::uint32_t, std::uint32_t, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_EnumValue(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t,
                                                      std::uint32_t, std::uint32_t, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_QueryValue(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t,
                                                       std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_SetValue(void*, void*, std::uint32_t, std::uint32_t, std::uint32_t,
                                                     std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_ValueExists(void*, void*, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_GetSubKeyCount(void*, void*);
std::uint32_t __fastcall Win32_CRegistryKey_GetValueCount(void*, void*);
std::uint32_t __fastcall Win32_CRegistryKey_QueryDword(void*, void*, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_SetDword(void*, void*, std::uint32_t, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_Destroy(void*, void*);
std::uint32_t __fastcall Win32_CRegistryKey_Copy(void*, void*, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_Assign(void*, void*, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_QueryString(void*, void*, std::uint32_t, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_SetString(void*, void*, std::uint32_t, msvc6::String, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_GetSubKey(void*, void*, std::uint32_t, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryKey_GetValueInfo(void*, void*, std::uint32_t, std::uint32_t);
std::uint32_t __fastcall Win32_CRegistryValueInfo_Destroy(void*, void*);
using SetStringFn = std::uint32_t(__fastcall*)(void*, void*, std::uint32_t, msvc6::String, std::uint32_t);

extern Member7Fn Original_Win32_CRegistryKey_Create;
extern MemberArgFn Original_Win32_CRegistryKey_DeleteSubKey;
extern Member3Fn Original_Win32_CRegistryKey_Open;
extern MemberFn Original_Win32_CRegistryKey_Close;
extern Member10Fn Original_Win32_CRegistryKey_QueryInfo;
extern Member6Fn Original_Win32_CRegistryKey_EnumKey;
extern Member6Fn Original_Win32_CRegistryKey_EnumValue;
extern Member4Fn Original_Win32_CRegistryKey_QueryValue;
extern Member4Fn Original_Win32_CRegistryKey_SetValue;
extern MemberArgFn Original_Win32_CRegistryKey_ValueExists;
extern MemberFn Original_Win32_CRegistryKey_GetSubKeyCount;
extern MemberFn Original_Win32_CRegistryKey_GetValueCount;
extern MemberArgFn Original_Win32_CRegistryKey_QueryDword;
extern Member2Fn Original_Win32_CRegistryKey_SetDword;
extern MemberFn Original_Win32_CRegistryKey_Destroy;
extern MemberArgFn Original_Win32_CRegistryKey_Copy;
extern MemberArgFn Original_Win32_CRegistryKey_Assign;
extern Member2Fn Original_Win32_CRegistryKey_QueryString;
extern SetStringFn Original_Win32_CRegistryKey_SetString;
extern Member2Fn Original_Win32_CRegistryKey_GetSubKey;
extern Member2Fn Original_Win32_CRegistryKey_GetValueInfo;
extern MemberFn Original_Win32_CRegistryValueInfo_Destroy;

namespace {

struct Functions {
    Member7Fn create;
    MemberArgFn deleteSubKey;
    Member3Fn open;
    MemberFn close;
    Member10Fn queryInfo;
    Member6Fn enumKey, enumValue;
    Member4Fn queryValue, setValue;
    MemberArgFn valueExists;
    MemberFn subKeyCount, valueCount;
    MemberArgFn queryDword;
    Member2Fn setDword;
    MemberFn destroy;
    MemberArgFn copy, assign;
    Member2Fn queryString;
    SetStringFn setString;
    Member2Fn getSubKey, getValueInfo;
    MemberFn destroyValueInfo;
};

Functions Originals() {
    return {Original_Win32_CRegistryKey_Create,        Original_Win32_CRegistryKey_DeleteSubKey,
            Original_Win32_CRegistryKey_Open,          Original_Win32_CRegistryKey_Close,
            Original_Win32_CRegistryKey_QueryInfo,     Original_Win32_CRegistryKey_EnumKey,
            Original_Win32_CRegistryKey_EnumValue,     Original_Win32_CRegistryKey_QueryValue,
            Original_Win32_CRegistryKey_SetValue,      Original_Win32_CRegistryKey_ValueExists,
            Original_Win32_CRegistryKey_GetSubKeyCount, Original_Win32_CRegistryKey_GetValueCount,
            Original_Win32_CRegistryKey_QueryDword,    Original_Win32_CRegistryKey_SetDword,
            Original_Win32_CRegistryKey_Destroy,       Original_Win32_CRegistryKey_Copy,
            Original_Win32_CRegistryKey_Assign,        Original_Win32_CRegistryKey_QueryString,
            Original_Win32_CRegistryKey_SetString,     Original_Win32_CRegistryKey_GetSubKey,
            Original_Win32_CRegistryKey_GetValueInfo,  Original_Win32_CRegistryValueInfo_Destroy};
}

Functions Ours() {
    return {&Win32_CRegistryKey_Create,        &Win32_CRegistryKey_DeleteSubKey, &Win32_CRegistryKey_Open,
            &Win32_CRegistryKey_Close,         &Win32_CRegistryKey_QueryInfo,    &Win32_CRegistryKey_EnumKey,
            &Win32_CRegistryKey_EnumValue,     &Win32_CRegistryKey_QueryValue,   &Win32_CRegistryKey_SetValue,
            &Win32_CRegistryKey_ValueExists,   &Win32_CRegistryKey_GetSubKeyCount,
            &Win32_CRegistryKey_GetValueCount, &Win32_CRegistryKey_QueryDword,   &Win32_CRegistryKey_SetDword,
            &Win32_CRegistryKey_Destroy,       &Win32_CRegistryKey_Copy,         &Win32_CRegistryKey_Assign,
            &Win32_CRegistryKey_QueryString,   &Win32_CRegistryKey_SetString,    &Win32_CRegistryKey_GetSubKey,
            &Win32_CRegistryKey_GetValueInfo,  &Win32_CRegistryValueInfo_Destroy};
}

// ---------------------------------------------------------------- the fakes

// The buffer: guard, key object, guard, the second key, a std::string, the
// area for texts and out-parameters.
constexpr std::size_t kGuard = 8;
constexpr std::size_t kSelf = kGuard;
constexpr std::size_t kOther = 0x20;
constexpr std::size_t kString = 0x30;
constexpr std::size_t kArea = 0x40;
constexpr std::size_t kInfo = 0x100;    // a CRegistryValueInfo (0x18 bytes)
constexpr std::size_t kResult = 0x120;  // the results of QueryString / GetSubKey / GetValueInfo
constexpr std::size_t kBytes = 0x140;
alignas(16) std::uint8_t g_buffer[kBytes];

// A pointer argument as logged: its offset in the buffer, or (a local of
// the caller, or a heap block: different in the two runs) its presence.
std::uint32_t Where(const void* p) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    if (b >= g_buffer && b < g_buffer + kBytes) {
        return 0x10000u + static_cast<std::uint32_t>(b - g_buffer);
    }
    return p != nullptr ? 1u : 0u;
}

struct Fake {
    test::Rng rng{0};
    std::vector<std::uint32_t> log;
    // GetSubKey: RegCloseKey must not fail, since it runs while an exception
    // unwinds the local key when the result's copy throws (a second throw
    // terminates the process, in the original as in ours).
    bool closeSucceeds = false;
    // GetValueInfo: no ERROR_NO_MORE_ITEMS from RegEnumValueA (it would go
    // on with an uninitialized type; docs/todo/known-differences.md).
    bool valueFound = false;
    std::int32_t Result() {
        if (!rng.Chance(1, 3)) {
            return 0;
        }
        static const std::int32_t kCodes[] = {2, 5, 234, 259, 6};
        return rng.Chance(1, 6) ? static_cast<std::int32_t>(rng.Next32()) : kCodes[rng.Below(5)];
    }
    void Log(std::initializer_list<std::uint32_t> values) { log.insert(log.end(), values); }
    void Write(std::uint32_t* p) {
        if (p != nullptr) {
            *p = rng.Chance(1, 2) ? rng.Below(64) : rng.Next32();
        }
    }
    // A name or data of up to *length bytes (at most 12), *length = the count.
    void WriteBytes(void* buffer, std::uint32_t* length) {
        if (length == nullptr) {
            return;
        }
        const std::uint32_t n = *length < 12 ? *length : 12;
        const std::uint32_t k = n == 0 ? 0 : rng.Below(n + 1);
        if (buffer != nullptr) {
            rng.Fill(static_cast<std::uint8_t*>(buffer), k);
        }
        *length = k;
    }
    // The same as a text: at least one byte, the last one zero (so that the
    // callers' strlen stays inside the buffer).
    void WriteText(char* buffer, std::uint32_t* length) {
        if (length == nullptr || *length == 0) {
            return;
        }
        const std::uint32_t n = *length < 12 ? *length : 12;
        const std::uint32_t k = 1 + rng.Below(n);
        if (buffer != nullptr) {
            for (std::uint32_t i = 0; i + 1 < k; ++i) {
                buffer[i] = static_cast<char>(1 + rng.Below(255));
            }
            buffer[k - 1] = 0;
        }
        *length = k;
    }
};
Fake* g_fake = nullptr;

std::uint32_t Ptr(const void* p) {
    return reinterpret_cast<std::uint32_t>(p);
}

std::int32_t __stdcall FakeOpen(std::uint32_t key, const char* subKey, std::uint32_t options, std::uint32_t sam,
                                std::uint32_t* result) {
    g_fake->Log({0x21u, key, Where(subKey), options, sam, Where(result)});
    const std::int32_t r = g_fake->Result();
    if (r == 0) {
        *result = g_fake->rng.Next32();
    }
    return r;
}
std::int32_t __stdcall FakeCreate(std::uint32_t key, const char* subKey, std::uint32_t reserved, const char* keyClass,
                                  std::uint32_t options, std::uint32_t sam, const void* security,
                                  std::uint32_t* result, std::uint32_t* disposition) {
    g_fake->Log({0x22u, key, Ptr(subKey), reserved, Ptr(keyClass), options, sam, Ptr(security), Ptr(result),
                 disposition != nullptr ? 1u : 0u});
    const std::int32_t r = g_fake->Result();
    if (r == 0) {
        *result = g_fake->rng.Next32();
        *disposition = 1 + g_fake->rng.Below(2);
    }
    return r;
}
std::int32_t __stdcall FakeClose(std::uint32_t key) {
    g_fake->Log({0x23u, key});
    const std::int32_t r = g_fake->Result();
    return g_fake->closeSucceeds ? 0 : r;
}
std::int32_t __stdcall FakeDelete(std::uint32_t key, const char* subKey) {
    g_fake->Log({0x24u, key, Ptr(subKey)});
    return g_fake->Result();
}
std::int32_t __stdcall FakeQueryInfo(std::uint32_t key, char* keyClass, std::uint32_t* classLength,
                                     std::uint32_t* reserved, std::uint32_t* subKeys, std::uint32_t* maxSubKeyLength,
                                     std::uint32_t* maxClassLength, std::uint32_t* values,
                                     std::uint32_t* maxValueNameLength, std::uint32_t* maxValueLength,
                                     std::uint32_t* securityDescriptorSize, void* lastWrite) {
    g_fake->Log({0x25u, key, Ptr(keyClass), Ptr(classLength), Ptr(reserved), Ptr(lastWrite)});
    // Local out-parameters (the counts) differ in address: logged by presence.
    for (const std::uint32_t* p :
         {subKeys, maxSubKeyLength, maxClassLength, values, maxValueNameLength, maxValueLength, securityDescriptorSize}) {
        g_fake->log.push_back(p != nullptr ? 1u : 0u);
    }
    const std::int32_t r = g_fake->Result();
    if (r == 0) {
        g_fake->WriteBytes(keyClass, classLength);
        for (std::uint32_t* p : {subKeys, maxSubKeyLength, maxClassLength, values, maxValueNameLength, maxValueLength,
                                 securityDescriptorSize}) {
            // GetSubKey / GetValueInfo allocate length + 1 bytes: a huge one
            // may or may not succeed depending on the address space, so the
            // out-of-memory path is left to the emulator spec (new_fails).
            if (p != nullptr && (g_fake->closeSucceeds || g_fake->valueFound)) {
                *p = g_fake->rng.Below(64);
            } else {
                g_fake->Write(p);
            }
        }
        if (lastWrite != nullptr) {
            g_fake->rng.Fill(static_cast<std::uint8_t*>(lastWrite), 8);
        }
    }
    return r;
}
std::int32_t __stdcall FakeEnumKey(std::uint32_t key, std::uint32_t index, char* name, std::uint32_t* nameLength,
                                   std::uint32_t* reserved, char* keyClass, std::uint32_t* classLength,
                                   void* lastWrite) {
    g_fake->Log({0x26u, key, index, Where(name), Where(nameLength), Ptr(reserved), Where(keyClass),
                 Where(classLength), lastWrite != nullptr ? 1u : 0u});
    g_fake->rng.Fill(static_cast<std::uint8_t*>(lastWrite), 8);  // always, see the file comment
    const std::int32_t r = g_fake->Result();
    if (r == 0) {
        g_fake->WriteBytes(name, nameLength);
        g_fake->WriteBytes(keyClass, classLength);
    }
    return r;
}
std::int32_t __stdcall FakeEnumValue(std::uint32_t key, std::uint32_t index, char* name, std::uint32_t* nameLength,
                                     std::uint32_t* reserved, std::uint32_t* type, void* data,
                                     std::uint32_t* dataSize) {
    g_fake->Log({0x27u, key, index, Where(name), Where(nameLength), Ptr(reserved), Where(type), Where(data),
                 Where(dataSize)});
    std::int32_t r = g_fake->Result();
    if (g_fake->valueFound && r == 259) {
        r = 0;
    }
    if (r == 0) {
        g_fake->WriteText(name, nameLength);
        g_fake->Write(type);
        g_fake->WriteBytes(data, dataSize);
    }
    return r;
}
std::int32_t __stdcall FakeQueryValue(std::uint32_t key, const char* name, std::uint32_t* reserved,
                                      std::uint32_t* type, void* data, std::uint32_t* dataSize) {
    // QueryDword's out-parameters are locals: logged by presence.
    g_fake->Log({0x28u, key, Where(name), Ptr(reserved), Where(type), Where(data),
                 dataSize != nullptr ? *dataSize : 0xFFFFFFFFu});
    const std::int32_t r = g_fake->Result();
    if (r == 0) {
        g_fake->Write(type);
        if (data == nullptr && dataSize != nullptr) {
            *dataSize = 1 + g_fake->rng.Below(12);  // the size query of QueryString
        } else {
            g_fake->WriteText(static_cast<char*>(data), dataSize);
        }
    }
    return r;
}
std::int32_t __stdcall FakeSetValue(std::uint32_t key, const char* name, std::uint32_t reserved, std::uint32_t type,
                                    const void* data, std::uint32_t size) {
    // SetDword's data is a stack slot: logged by contents.
    std::uint32_t first = 0;
    if (data != nullptr && size >= 4) {
        std::memcpy(&first, data, 4);
    }
    g_fake->Log({0x29u, key, Ptr(name), reserved, type, first, size});
    return g_fake->Result();
}

struct Slot {
    std::uintptr_t address;
    std::uintptr_t fake;
};

class InstalledFakes {
public:
    explicit InstalledFakes(Fake* fake) {
        g_fake = fake;
        const Slot slots[] = {
            {g_importRegOpenKeyExA.address(), reinterpret_cast<std::uintptr_t>(&FakeOpen)},
            {g_importRegCreateKeyExA.address(), reinterpret_cast<std::uintptr_t>(&FakeCreate)},
            {g_importRegCloseKey.address(), reinterpret_cast<std::uintptr_t>(&FakeClose)},
            {g_importRegDeleteKeyA.address(), reinterpret_cast<std::uintptr_t>(&FakeDelete)},
            {g_importRegQueryInfoKeyA.address(), reinterpret_cast<std::uintptr_t>(&FakeQueryInfo)},
            {g_importRegEnumKeyExA.address(), reinterpret_cast<std::uintptr_t>(&FakeEnumKey)},
            {g_importRegEnumValueA.address(), reinterpret_cast<std::uintptr_t>(&FakeEnumValue)},
            {g_importRegQueryValueExA.address(), reinterpret_cast<std::uintptr_t>(&FakeQueryValue)},
            {g_importRegSetValueExA.address(), reinterpret_cast<std::uintptr_t>(&FakeSetValue)},
        };
        for (const Slot& slot : slots) {
            test::PatchBytes(slot.address, &slot.fake, 4);
            patched_.push_back(slot.address);
        }
    }
    ~InstalledFakes() {
        for (auto it = patched_.rbegin(); it != patched_.rend(); ++it) {
            test::UnpatchBytes(*it);
        }
        g_fake = nullptr;
    }
    InstalledFakes(const InstalledFakes&) = delete;
    InstalledFakes& operator=(const InstalledFakes&) = delete;

private:
    std::vector<std::uintptr_t> patched_;
};

// ---------------------------------------------------------------- one case

struct Caught {
    bool threw = false;
    std::uint32_t vtable = 0;
    std::uint32_t info = 0;
    std::uint32_t code = 0;
    std::string message;
    bool operator==(const Caught& o) const {
        return threw == o.threw && vtable == o.vtable && info == o.info && code == o.code && message == o.message;
    }
};

int CatchFilter(const EXCEPTION_POINTERS* info, Caught* out) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    if (record->ExceptionCode != 0xE06D7363 || record->NumberParameters < 3) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    out->threw = true;
    out->info = static_cast<std::uint32_t>(record->ExceptionInformation[2]);
    void* const object = *reinterpret_cast<void* const*>(record->ExceptionInformation[1]);
    if (object != nullptr) {
        const auto* bytes = static_cast<const std::uint8_t*>(object);
        std::memcpy(&out->vtable, bytes, 4);
        std::memcpy(&out->code, bytes + 0x18, 4);  // CHResult's code
        const auto* message = reinterpret_cast<const msvc6::String*>(bytes + 8);
        if (message->ptr != nullptr) {
            out->message.assign(message->ptr, message->len);
        }
    }
    msvc6::DeleteCaughtException(object);
    return EXCEPTION_EXECUTE_HANDLER;
}


struct Outcome {
    std::uint32_t eax = 0;
    Caught caught;
    std::vector<std::uint32_t> log;
    std::uint8_t bytes[kBytes];
};

struct Args {
    std::uint32_t op;
    std::uint32_t a[10];
    std::string text;      // SetString's value / the value info's name
    std::uint8_t count;    // its buffer's count byte
};

// A heap string (the game's operator new) with `text`, its count byte set.
msvc6::String HeapString(const Args& args) {
    msvc6::String s{};
    msvc6::String_Tidy(&s, nullptr, 0);
    if (!args.text.empty()) {
        msvc6::String_AssignChars(&s, nullptr, args.text.c_str(), static_cast<std::uint32_t>(args.text.size()));
        s.ptr[-1] = static_cast<char>(args.count);
    }
    return s;
}

std::uint32_t Call(const Functions& f, void* self, const Args& c) {
    const std::uint32_t* a = c.a;
    switch (c.op) {
        case 0: return f.create(self, nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
        case 1: return f.deleteSubKey(self, nullptr, a[0]);
        case 2: return f.open(self, nullptr, a[0], a[1], a[2]);
        case 3: return f.close(self, nullptr);
        case 4: return f.queryInfo(self, nullptr, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
        case 5: return f.enumKey(self, nullptr, a[0], a[1], a[2], a[3], a[4], a[5]);
        case 6: return f.enumValue(self, nullptr, a[0], a[1], a[2], a[3], a[4], a[5]);
        case 7: return f.queryValue(self, nullptr, a[0], a[1], a[2], a[3]);
        case 8: return f.setValue(self, nullptr, a[0], a[1], a[2], a[3]);
        case 9: return f.valueExists(self, nullptr, a[0]);
        case 10: return f.subKeyCount(self, nullptr);
        case 11: return f.valueCount(self, nullptr);
        case 12: return f.queryDword(self, nullptr, a[0]);
        case 13: return f.setDword(self, nullptr, a[0], a[1]);
        case 14: return f.destroy(self, nullptr);
        case 15: return f.copy(self, nullptr, a[0]);
        case 16: return f.assign(self, nullptr, a[0]);
        case 17: return f.queryString(self, nullptr, Ptr(g_buffer + kResult), a[0]);
        case 18: return f.setString(self, nullptr, a[0], HeapString(c), a[1]);
        case 19: return f.getSubKey(self, nullptr, Ptr(g_buffer + kResult), a[0]);
        case 20: return f.getValueInfo(self, nullptr, Ptr(g_buffer + kResult), a[0]);
        default: return f.destroyValueInfo(g_buffer + kInfo, nullptr);
    }
}

// The call under SEH (in a function of its own: no objects to unwind).
std::uint32_t Guarded(const Functions& f, void* self, const Args& args, Caught* caught) {
    std::uint32_t eax = 0;
    __try {
        eax = Call(f, self, args);
    } __except (CatchFilter(GetExceptionInformation(), caught)) {
    }
    return eax;
}

Outcome RunOne(const Functions& f, const std::uint8_t* initial, std::uint64_t seed, const Args& args) {
    Outcome out;
    std::memcpy(g_buffer, initial, kBytes);
    Fake fake;
    fake.rng = test::Rng(seed);
    fake.closeSucceeds = args.op == 19;
    fake.valueFound = args.op == 20;
    if (args.op == 21) {
        const msvc6::String name = HeapString(args);
        std::memcpy(g_buffer + kInfo + 4, &name, sizeof(name));
    }
    {
        InstalledFakes installed(&fake);
        out.eax = Guarded(f, g_buffer + kSelf, args, &out.caught);
    }
    // Strings with heap buffers (their addresses differ between the runs):
    // compared by text, then freed or released, and their pointer cleared.
    auto takeString = [&](std::size_t at) {
        auto* s = reinterpret_cast<msvc6::String*>(g_buffer + at);
        if (s->ptr == nullptr) {
            out.log.push_back(0xDEADu);
            return;
        }
        out.log.push_back(static_cast<std::uint8_t>(s->ptr[-1]));
        for (std::uint32_t i = 0; i < s->len; ++i) {
            out.log.push_back(static_cast<std::uint8_t>(s->ptr[i]));
        }
        msvc6::String_Tidy(s, nullptr, 1);
        s->allocator = g_buffer[at];
    };
    if (!out.caught.threw && (args.op == 17 || args.op == 20)) {
        takeString(args.op == 17 ? kResult : kResult + 4);
    }
    std::memcpy(out.bytes, g_buffer, kBytes);
    if (args.op == 20) {
        // GetValueInfo's result name gets the low byte of a stack address as
        // its allocator byte (see win32_registry.cpp); the frames of the two
        // runs differ here, the emulator spec compares it.
        out.bytes[kResult + 4] = 0;
    }
    out.log.insert(out.log.begin(), fake.log.begin(), fake.log.end());
    if (args.op == 18 || args.op == 21) {
        out.eax = 0;  // EAX is not used by the callers
    }
    return out;
}

// A pointer into the area (4-aligned), or null now and then.
std::uint32_t AreaPointer(test::Rng& rng, bool nullable = true) {
    if (nullable && rng.Chance(1, 4)) {
        return 0;
    }
    return Ptr(g_buffer + kArea + 4 * rng.Below((kBytes - kArea - 16) / 4));
}

std::uint32_t Number(test::Rng& rng) {
    return rng.Chance(1, 2) ? rng.Below(16) : rng.Next32();
}

Args MakeArgs(test::Rng& rng) {
    Args args{};
    args.op = rng.Below(22);
    args.text.assign(rng.Below(3) == 0 ? 0 : rng.Below(24), 'x');
    for (char& ch : args.text) {
        ch = static_cast<char>(1 + rng.Below(255));
    }
    static const std::uint8_t kCounts[] = {0, 0, 1, 2, 0xFE, 0xFF};
    args.count = kCounts[rng.Below(6)];
    for (std::uint32_t& a : args.a) {
        a = AreaPointer(rng);
    }
    switch (args.op) {
        case 0:  // parent, subKey, class, options, sam, security, disposition*
            args.a[0] = Number(rng);
            args.a[3] = Number(rng);
            args.a[4] = Number(rng);
            args.a[5] = rng.Chance(1, 2) ? 0 : rng.Next32();
            break;
        case 2:
            args.a[0] = Number(rng);
            args.a[2] = Number(rng);
            break;
        case 5:
        case 6:
            args.a[0] = Number(rng);
            break;
        case 8:
            args.a[1] = Number(rng);
            args.a[3] = Number(rng);
            break;
        case 12:
        case 13: {
            args.a[0] = Ptr(g_buffer + kString);
            args.a[1] = rng.Next32();
            break;
        }
        case 15:
            args.a[0] = Ptr(g_buffer + kOther);
            break;
        case 17:
            args.a[0] = Ptr(g_buffer + kString);
            break;
        case 18:
            args.a[0] = Ptr(g_buffer + kString);
            args.a[1] = rng.Chance(1, 2) ? rng.Below(2) : rng.Next32();
            break;
        case 19:
        case 20:
            args.a[0] = Number(rng);
            break;
        case 16:
            args.a[0] = Ptr(g_buffer + (rng.Chance(1, 4) ? kSelf : kOther));
            break;
        default: break;
    }
    return args;
}

void Compare(test::Context& ctx, const Functions& ours, int cases) {
    const Functions originals = Originals();
    for (int i = 0; i < cases; ++i) {
        test::Rng& rng = ctx.rng();
        std::uint8_t initial[kBytes];
        rng.Fill(initial, sizeof(initial));
        // The keys: open (any HKEY) or not.
        const std::uint32_t self = rng.Chance(1, 4) ? 0 : rng.Next32();
        const std::uint32_t other = rng.Chance(1, 4) ? 0 : rng.Next32();
        std::memcpy(initial + kSelf + 4, &self, 4);
        std::memcpy(initial + kOther + 4, &other, 4);
        // The string: no buffer, or a text in the area.
        const std::uint32_t text = rng.Chance(1, 4) ? 0 : Ptr(g_buffer + kArea + 0x80);
        std::memcpy(initial + kString + 4, &text, 4);
        // Some length fields small enough to be written to.
        for (std::size_t at = kArea; at + 4 <= kBytes; at += 4) {
            if (rng.Chance(1, 2)) {
                const std::uint32_t small = rng.Below(20);
                std::memcpy(initial + at, &small, 4);
            }
        }
        const Args args = MakeArgs(rng);
        const std::uint64_t seed = rng.Next64();
        const Outcome a = RunOne(originals, initial, seed, args);
        const Outcome b = RunOne(ours, initial, seed, args);
        // A throw leaves EAX undefined.
        const bool same = S3RE_EXPECT_EQ(a.caught.threw ? 0 : a.eax, b.caught.threw ? 0 : b.eax) &&
                          S3RE_EXPECT(a.caught == b.caught) && S3RE_EXPECT(a.log == b.log) &&
                          S3RE_EXPECT_MEM_EQ(a.bytes, b.bytes, sizeof(a.bytes));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d: op %u, threw %d/%d, message '%s' / '%s'", i, args.op,
                     a.caught.threw ? 1 : 0, b.caught.threw ? 1 : 0, a.caught.message.c_str(),
                     b.caught.message.c_str());
            return;
        }
    }
}

// Wrong: ValueExists treats every error as "not there".
std::uint32_t __fastcall WrongValueExists(void* self, void* edx, std::uint32_t name) {
    __try {
        return Win32_CRegistryKey_ValueExists(self, edx, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Wrong: EnumKey returns the flag in all of EAX.
std::uint32_t __fastcall WrongEnumKey(void* self, void* edx, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                      std::uint32_t d, std::uint32_t e, std::uint32_t f) {
    return Win32_CRegistryKey_EnumKey(self, edx, a, b, c, d, e, f) & 0xFF;
}

}  // namespace

S3RE_TEST(Win32Registry_MatchOriginal) {
    Compare(ctx, Ours(), 20000);
}

S3RE_TEST(Win32Registry_ComparisonDetectsWrongImplementation) {
    Functions wrong = Ours();
    wrong.valueExists = &WrongValueExists;
    {
        test::Context probe(ctx.name(), 1, /*quiet=*/true);
        Compare(probe, wrong, 2000);
        S3RE_EXPECT(probe.failures() > 0);
    }
    wrong = Ours();
    wrong.enumKey = &WrongEnumKey;
    {
        test::Context probe(ctx.name(), 2, /*quiet=*/true);
        Compare(probe, wrong, 2000);
        S3RE_EXPECT(probe.failures() > 0);
    }
}

}  // namespace s3re::game
