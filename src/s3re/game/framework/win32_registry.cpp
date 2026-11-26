// Win32::CRegistryKey, the framework's wrapper of a registry key
// (Win32\Registry.cpp; RTTI class name; wip/app-r4). The object is a vtable
// (0x65d98c) and the HKEY at +4 (0: no key open). The game reads and writes
// its settings under HKLM\Software\BlueByte\Siedler3\1.0 through it.
//
// Every member makes one registry call and checks its result (a Windows
// error code) the framework's way: on failure
// Win32::ThrowWin32Error("error check", Registry.cpp, line, result,
// location), with the empty location string built as in
// win32_synchronize.cpp (from "" 0x7a7868 through two temporaries). The code
// after the (never returning) throw destroys the location again.
//
//   Create 0x6164a0 (parent, subKey, keyClass, options, sam, security,
//     disposition*) ret 1Ch: RegCreateKeyExA(parent, subKey, 0, keyClass,
//     options, sam, security, &hkey, &disposition), line 48; the
//     disposition lives in the stack slot of `security`; when the pointer
//     is given it gets the disposition. EAX: 0 (the result) or the pointer.
//   DeleteSubKey 0x6166e0 (subKey) ret 4: RegDeleteKeyA(hkey, subKey), line
//     62. EAX: 0.
//   Open 0x616900 (parent, subKey, sam) ret 0Ch: RegOpenKeyExA(parent,
//     subKey, 0, sam, &hkey), line 74. EAX: 0.
//   Close 0x616b20 (): when a key is open RegCloseKey(hkey), line 85; then
//     hkey = 0. EAX: 0.
//   QueryInfo 0x616d50 (the ten out pointers of RegQueryInfoKeyA, without
//     lpReserved) ret 28h: RegQueryInfoKeyA(hkey, a1, a2, NULL, a3..a10),
//     line 126. EAX: 0.
//   EnumKey 0x616f90 (index, name, nameLength*, keyClass, classLength*,
//     lastWrite*) ret 18h: RegEnumKeyExA(hkey, index, name, nameLength, NULL,
//     keyClass, classLength, &local FILETIME); ERROR_NO_MORE_ITEMS (259):
//     false, another error: throw (line 153); then the FILETIME is copied
//     to lastWrite when given, also after ERROR_NO_MORE_ITEMS (the local
//     is then uninitialized in the original; ours is zeroed; every caller
//     passes null). AL: the flag, the rest of EAX: lastWrite's upper bytes.
//   EnumValue 0x6175c0 (index, name, nameLength*, type*, data, dataSize*)
//     ret 18h: RegEnumValueA(hkey, index, name, nameLength, NULL, type,
//     data, dataSize); ERROR_NO_MORE_ITEMS: false (EAX 0x100, the rest of
//     259), another error: throw (line 250), else true (EAX 1).
//   QueryValue 0x617810 (name, type*, data, dataSize*) ret 10h:
//     RegQueryValueExA(hkey, name, NULL, type, data, dataSize), line 297.
//     EAX: 0.
//   SetValue 0x617c80 (name, type, data, size) ret 10h: RegSetValueExA(hkey,
//     name, 0, type, data, size), line 367. EAX: 0.
//   ValueExists 0x618030 (name) ret 4: RegQueryValueExA(hkey, name, NULL,
//     NULL, NULL, NULL): success true (EAX 1), ERROR_FILE_NOT_FOUND (2)
//     false (EAX 0), another error: throw (line 418).
//   GetSubKeyCount 0x6171f0 / GetValueCount 0x617320 (): QueryInfo with
//     only the sub-key count / value count pointer (an uninitialized local),
//     returns it.
//   QueryDword 0x617c40 (const std::string& name) ret 4: QueryValue(c_str,
//     &type, &value, &size = 4) with the value in the stack slot of `name`
//     (so a value shorter than 4 bytes keeps the pointer's upper bytes);
//     returns it.
//   SetDword 0x618000 (const std::string& name, value) ret 8: SetValue(
//     c_str, REG_DWORD, &value, 4). EAX: SetValue's.
//   ~CRegistryKey 0x616490: vtable = CRegistryKey's, then (jmp) Close.
//   CRegistryKey(const CRegistryKey&) 0x616460 ret 4: hkey = 0, vtable; when
//     the other key is open, Open(other.hkey, NULL, KEY_ALL_ACCESS) (a second
//     handle to the same key). EAX: this.
//   operator= 0x618260 (other) ret 4: unless other is this: Close(), then
//     Open(other.hkey, NULL, KEY_ALL_ACCESS) when the other key is open.
//     EAX: this.
//
//   QueryString 0x617a40 (std::string* result, const std::string& name) ret
//     8: QueryValue(c_str, &type, NULL, &size) for the size, a buffer of that
//     size (operator new; none: ThrowOutOfMemory("lpData", line 312)),
//     QueryValue(c_str, NULL, buffer, &size), then text = string(buffer)
//     through a temporary, the buffer freed, *result = text, text destroyed
//     (erase, then free). The strings' allocator bytes are the low byte of
//     `name`'s address (the result keeps it). EAX: result.
//   SetString 0x617eb0 (const std::string& name, std::string value by value,
//     bool expand) ret 18h: SetValue(c_str, expand ? REG_EXPAND_SZ :
//     REG_SZ, value.c_str, value.size() + 1), then the value argument is
//     destroyed (erase, then free). EAX is not used by the callers.
//   GetSubKey 0x617220 (CRegistryKey* result, index) ret 8: a local key
//     (constructor 0x616430), QueryInfo for the longest sub-key name, a
//     buffer one longer (none: ThrowOutOfMemory("name", line 182)),
//     EnumKey(index, buffer, &length, NULL, NULL, NULL), the local key opened
//     with Open(hkey, buffer, KEY_ALL_ACCESS), the buffer freed, *result
//     copy-constructed (0x616460) from the local key, which is destroyed
//     (also when a call throws). EAX: result.
//   GetValueInfo 0x617350 (CRegistryValueInfo* result, index) ret 8: QueryInfo
//     for the longest value name, a buffer one longer (none:
//     ThrowOutOfMemory("lpName", line 216)), EnumValue(index, buffer,
//     &length, &type, NULL, NULL), name = string(buffer) through a temporary,
//     the buffer freed; a local CRegistryValueInfo(copy of name, type)
//     (0x6161e0; the copy by 0x408cc0), *result = {its name (assign), its
//     value} with the vtable, the local destroyed (0x616320), name destroyed.
//     The allocator bytes are the low byte of `index`, the result's that of
//     the temporary's address (stored over `index` by then). EAX: result.
//   ~CRegistryValueInfo 0x616320: vtable = CRegistryValueInfo's, the name
//     destroyed (erase, then free). EAX is not used by the callers.
//
// Allocator bytes: the location string's comes from a stack byte of the
// original (an argument's or a local's); it is never copied, ours use 0.

#include "game/framework/exception_strings.h"
#include "game/framework/framework_layout.h"
#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "game/vtables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"

#include <intrin.h>

#include <cstdint>
#include <cstring>

namespace s3re::game {

namespace {

using framework::CRegistryKey;
using msvc6::String;

constexpr std::uintptr_t kThrowWin32Error = 0x00610de0;
constexpr std::uintptr_t kRegistryKeyOpen = 0x00616900;
constexpr std::uintptr_t kRegistryKeyClose = 0x00616b20;
constexpr std::uintptr_t kRegistryKeyQueryInfo = 0x00616d50;
constexpr std::uintptr_t kRegistryKeyQueryValue = 0x00617810;
constexpr std::uintptr_t kRegistryKeySetValue = 0x00617c80;
constexpr std::uintptr_t kRegistryKeyEnumKey = 0x00616f90;
constexpr std::uintptr_t kRegistryKeyEnumValue = 0x006175c0;
constexpr std::uintptr_t kRegistryKeyConstruct = 0x00616430;
constexpr std::uintptr_t kRegistryKeyCopy = 0x00616460;
constexpr std::uintptr_t kRegistryKeyDestroy = 0x00616490;
constexpr std::uintptr_t kValueInfoConstruct = 0x006161e0;
constexpr std::uintptr_t kValueInfoDestroy = 0x00616320;
constexpr std::uintptr_t kThrowOutOfMemory = 0x00610960;  // Win32::ThrowOutOfMemory (never returns)

constexpr std::int32_t kErrorFileNotFound = 2;   // ERROR_FILE_NOT_FOUND
constexpr std::int32_t kErrorNoMoreItems = 259;  // ERROR_NO_MORE_ITEMS
constexpr std::uint32_t kKeyAllAccess = 0xF003F;  // KEY_ALL_ACCESS (an access mask, raw-ok)
constexpr std::uint32_t kRegDword = 4;            // REG_DWORD
constexpr std::uint32_t kRegSz = 1;               // REG_SZ
constexpr std::uint32_t kRegExpandSz = 2;         // REG_EXPAND_SZ
constexpr std::uint32_t kDwordSize = 4;

// Source lines of the checks (Win32\Registry.cpp).
constexpr std::uint32_t kLineCreate = 48;
constexpr std::uint32_t kLineDeleteSubKey = 62;
constexpr std::uint32_t kLineOpen = 74;
constexpr std::uint32_t kLineClose = 85;
constexpr std::uint32_t kLineQueryInfo = 126;
constexpr std::uint32_t kLineEnumKey = 153;
constexpr std::uint32_t kLineEnumValue = 250;
constexpr std::uint32_t kLineQueryValue = 297;
constexpr std::uint32_t kLineSetValue = 367;
constexpr std::uint32_t kLineValueExists = 418;
constexpr std::uint32_t kLineSubKeyName = 182;  // GetSubKey's buffer
constexpr std::uint32_t kLineValueName = 216;   // GetValueInfo's buffer
constexpr std::uint32_t kLineValueData = 312;   // QueryString's buffer

using ThrowWin32ErrorFn = void(__fastcall*)(const char* expression, const char* file, std::uint32_t line,
                                            std::uint32_t error, const String* location);
using OpenFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t parent, const char* subKey,
                                          std::uint32_t sam);
using CloseFn = std::uint32_t(__fastcall*)(void* self, void* edx);
using QueryInfoFn = std::uint32_t(__fastcall*)(void* self, void* edx, char* keyClass, std::uint32_t* classLength,
                                               std::uint32_t* subKeys, std::uint32_t* maxSubKeyLength,
                                               std::uint32_t* maxClassLength, std::uint32_t* values,
                                               std::uint32_t* maxValueNameLength, std::uint32_t* maxValueLength,
                                               std::uint32_t* securityDescriptorSize, void* lastWrite);
using QueryValueFn = std::uint32_t(__fastcall*)(void* self, void* edx, const char* name, std::uint32_t* type,
                                                void* data, std::uint32_t* dataSize);
using SetValueFn = std::uint32_t(__fastcall*)(void* self, void* edx, const char* name, std::uint32_t type,
                                              const void* data, std::uint32_t size);

using EnumKeyFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t index, char* name,
                                             std::uint32_t* nameLength, char* keyClass, std::uint32_t* classLength,
                                             void* lastWrite);
using EnumValueFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t index, char* name,
                                               std::uint32_t* nameLength, std::uint32_t* type, void* data,
                                               std::uint32_t* dataSize);
using CopyFn = std::uint32_t(__fastcall*)(void* self, void* edx, const void* other);
using ThrowOutOfMemoryFn = void(__fastcall*)(const char* name, const char* file, std::uint32_t line);
// CRegistryValueInfo(std::string name by value, value) (0x6161e0), ret 14h.
using ValueInfoConstructFn = void*(__fastcall*)(void* self, void* edx, String name, std::uint32_t value);

CRegistryKey* KeyOf(void* self) {
    return static_cast<CRegistryKey*>(self);
}

// The check failed: ThrowWin32Error with the registry call's result.
void ThrowRegistryError(std::int32_t result, std::uint32_t line) {
    String location;
    framework::MakeEmpty(&location, 0);
    framework::AssignThroughAssigns(&location, texts::kEmpty.c_str(), 0);
    reinterpret_cast<ThrowWin32ErrorFn>(kThrowWin32Error)(texts::kErrorCheck.c_str(), texts::kRegistryFile.c_str(),
                                                          line, static_cast<std::uint32_t>(result), &location);
    framework::EraseAndFree(&location);  // not reached: ThrowWin32Error throws
}

// The check of a call that must succeed; EAX of the members is the result.
std::uint32_t Check(std::int32_t result, std::uint32_t line) {
    if (result != 0) {
        ThrowRegistryError(result, line);
    }
    return static_cast<std::uint32_t>(result);
}

// c_str() of a std::string: its text, or "" when it has no buffer.
const char* CStrOf(const String* s) {
    return s->ptr != nullptr ? s->ptr : g_stringNullBuffer.get();
}

// Opens a second handle to `other`'s key into `self` (the copy and the
// assignment).
void Reopen(void* self, const void* other) {
    const std::uint32_t key = static_cast<const CRegistryKey*>(other)->key;
    if (key != 0) {
        reinterpret_cast<OpenFn>(kRegistryKeyOpen)(self, nullptr, key, nullptr, kKeyAllAccess);
    }
}

// A buffer from operator new; none: ThrowOutOfMemory(name, Registry.cpp,
// line).
char* NewBuffer(std::uint32_t size, const GameText& name, std::uint32_t line) {
    auto* buffer = static_cast<char*>(msvc6::GameOperatorNew(size));
    if (buffer == nullptr) {
        reinterpret_cast<ThrowOutOfMemoryFn>(kThrowOutOfMemory)(name.c_str(), texts::kRegistryFile.c_str(), line);
    }
    return buffer;
}

// `target` = string(text) through a temporary: _Tidy + assign(text, n),
// then target.assign(temporary, 0, npos), the temporary freed.
void AssignThroughTemporary(String* target, const char* text, std::uint8_t allocator) {
    String temporary;
    framework::MakeEmpty(&temporary, allocator);
    msvc6::String_AssignChars(&temporary, nullptr, text, msvc6::detail::Length(text));
    msvc6::String_AssignSubstr(target, nullptr, &temporary, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&temporary, nullptr, 1);
}

// GetSubKey's local key: destroyed (0x616490) when the function returns or
// a call throws, as the original's exception frame does.
struct LocalKey {
    CRegistryKey key;
    LocalKey() { reinterpret_cast<CloseFn>(kRegistryKeyConstruct)(&key, nullptr); }
    ~LocalKey() { reinterpret_cast<CloseFn>(kRegistryKeyDestroy)(&key, nullptr); }
    LocalKey(const LocalKey&) = delete;
    LocalKey& operator=(const LocalKey&) = delete;
};

}  // namespace

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

std::uint32_t __fastcall Win32_CRegistryKey_Create(void* self, void* edx, std::uint32_t parent, std::uint32_t subKey,
                                                   std::uint32_t keyClass, std::uint32_t options, std::uint32_t sam,
                                                   std::uint32_t security, std::uint32_t disposition);
std::uint32_t __fastcall Win32_CRegistryKey_DeleteSubKey(void* self, void* edx, std::uint32_t subKey);
std::uint32_t __fastcall Win32_CRegistryKey_Open(void* self, void* edx, std::uint32_t parent, std::uint32_t subKey,
                                                 std::uint32_t sam);
std::uint32_t __fastcall Win32_CRegistryKey_Close(void* self, void* edx);
std::uint32_t __fastcall Win32_CRegistryKey_QueryInfo(void* self, void* edx, std::uint32_t a1, std::uint32_t a2,
                                                      std::uint32_t a3, std::uint32_t a4, std::uint32_t a5,
                                                      std::uint32_t a6, std::uint32_t a7, std::uint32_t a8,
                                                      std::uint32_t a9, std::uint32_t a10);
std::uint32_t __fastcall Win32_CRegistryKey_EnumKey(void* self, void* edx, std::uint32_t index, std::uint32_t name,
                                                    std::uint32_t nameLength, std::uint32_t keyClass,
                                                    std::uint32_t classLength, std::uint32_t lastWrite);
std::uint32_t __fastcall Win32_CRegistryKey_EnumValue(void* self, void* edx, std::uint32_t index, std::uint32_t name,
                                                      std::uint32_t nameLength, std::uint32_t type,
                                                      std::uint32_t data, std::uint32_t dataSize);
std::uint32_t __fastcall Win32_CRegistryKey_QueryValue(void* self, void* edx, std::uint32_t name, std::uint32_t type,
                                                       std::uint32_t data, std::uint32_t dataSize);
std::uint32_t __fastcall Win32_CRegistryKey_SetValue(void* self, void* edx, std::uint32_t name, std::uint32_t type,
                                                     std::uint32_t data, std::uint32_t size);
std::uint32_t __fastcall Win32_CRegistryKey_ValueExists(void* self, void* edx, std::uint32_t name);
std::uint32_t __fastcall Win32_CRegistryKey_GetSubKeyCount(void* self, void* edx);
std::uint32_t __fastcall Win32_CRegistryKey_GetValueCount(void* self, void* edx);
std::uint32_t __fastcall Win32_CRegistryKey_QueryDword(void* self, void* edx, std::uint32_t name);
std::uint32_t __fastcall Win32_CRegistryKey_SetDword(void* self, void* edx, std::uint32_t name, std::uint32_t value);
std::uint32_t __fastcall Win32_CRegistryKey_Destroy(void* self, void* edx);
std::uint32_t __fastcall Win32_CRegistryKey_Copy(void* self, void* edx, std::uint32_t other);
std::uint32_t __fastcall Win32_CRegistryKey_Assign(void* self, void* edx, std::uint32_t other);
std::uint32_t __fastcall Win32_CRegistryKey_QueryString(void* self, void* edx, std::uint32_t result,
                                                        std::uint32_t name);
std::uint32_t __fastcall Win32_CRegistryKey_SetString(void* self, void* edx, std::uint32_t name, String value,
                                                      std::uint32_t expand);
std::uint32_t __fastcall Win32_CRegistryKey_GetSubKey(void* self, void* edx, std::uint32_t result,
                                                      std::uint32_t index);
std::uint32_t __fastcall Win32_CRegistryKey_GetValueInfo(void* self, void* edx, std::uint32_t result,
                                                         std::uint32_t index);
std::uint32_t __fastcall Win32_CRegistryValueInfo_Destroy(void* self, void* edx);

S3RE_HOOK(0x006164a0, Win32_CRegistryKey_Create, Member7Fn, Original_Win32_CRegistryKey_Create,
          Win32_CRegistryKey_Create);

// Purpose: creates (or opens) a registry key below `parent`.
std::uint32_t __fastcall Win32_CRegistryKey_Create(void* self, void* /*edx*/, std::uint32_t parent,
                                                   std::uint32_t subKey, std::uint32_t keyClass,
                                                   std::uint32_t options, std::uint32_t sam, std::uint32_t security,
                                                   std::uint32_t disposition) {
    // The original lets RegCreateKeyExA write the disposition over its
    // `security` argument.
    std::uint32_t created = security;
    const std::int32_t result = g_importRegCreateKeyExA.get()(
        parent, reinterpret_cast<const char*>(subKey), 0, reinterpret_cast<const char*>(keyClass), options, sam,
        reinterpret_cast<const void*>(security), &KeyOf(self)->key, &created);
    Check(result, kLineCreate);
    if (disposition != 0) {
        *reinterpret_cast<std::uint32_t*>(disposition) = created;
        return disposition;
    }
    return 0;
}

S3RE_HOOK(0x006166e0, Win32_CRegistryKey_DeleteSubKey, MemberArgFn, Original_Win32_CRegistryKey_DeleteSubKey,
          Win32_CRegistryKey_DeleteSubKey);

// Purpose: deletes a sub key of this key.
std::uint32_t __fastcall Win32_CRegistryKey_DeleteSubKey(void* self, void* /*edx*/, std::uint32_t subKey) {
    const std::int32_t result =
        g_importRegDeleteKeyA.get()(KeyOf(self)->key, reinterpret_cast<const char*>(subKey));
    return Check(result, kLineDeleteSubKey);
}

S3RE_HOOK(0x00616900, Win32_CRegistryKey_Open, Member3Fn, Original_Win32_CRegistryKey_Open,
          Win32_CRegistryKey_Open);

// Purpose: opens an existing registry key below `parent`.
std::uint32_t __fastcall Win32_CRegistryKey_Open(void* self, void* /*edx*/, std::uint32_t parent,
                                                 std::uint32_t subKey, std::uint32_t sam) {
    const std::int32_t result =
        g_importRegOpenKeyExA.get()(parent, reinterpret_cast<const char*>(subKey), 0, sam, &KeyOf(self)->key);
    return Check(result, kLineOpen);
}

S3RE_HOOK(0x00616b20, Win32_CRegistryKey_Close, MemberFn, Original_Win32_CRegistryKey_Close,
          Win32_CRegistryKey_Close);

// Purpose: closes the key, if one is open.
std::uint32_t __fastcall Win32_CRegistryKey_Close(void* self, void* /*edx*/) {
    CRegistryKey* const key = KeyOf(self);
    if (key->key == 0) {
        return 0;
    }
    Check(g_importRegCloseKey.get()(key->key), kLineClose);
    key->key = 0;
    return 0;
}

S3RE_HOOK(0x00616d50, Win32_CRegistryKey_QueryInfo, Member10Fn, Original_Win32_CRegistryKey_QueryInfo,
          Win32_CRegistryKey_QueryInfo);

// Purpose: asks for the key's sizes and counts (sub keys, values, longest
// names).
std::uint32_t __fastcall Win32_CRegistryKey_QueryInfo(void* self, void* /*edx*/, std::uint32_t a1, std::uint32_t a2,
                                                      std::uint32_t a3, std::uint32_t a4, std::uint32_t a5,
                                                      std::uint32_t a6, std::uint32_t a7, std::uint32_t a8,
                                                      std::uint32_t a9, std::uint32_t a10) {
    auto out = [](std::uint32_t pointer) { return reinterpret_cast<std::uint32_t*>(pointer); };
    const std::int32_t result = g_importRegQueryInfoKeyA.get()(
        KeyOf(self)->key, reinterpret_cast<char*>(a1), out(a2), nullptr, out(a3), out(a4), out(a5), out(a6), out(a7),
        out(a8), out(a9), reinterpret_cast<void*>(a10));
    return Check(result, kLineQueryInfo);
}

S3RE_HOOK(0x00616f90, Win32_CRegistryKey_EnumKey, Member6Fn, Original_Win32_CRegistryKey_EnumKey,
          Win32_CRegistryKey_EnumKey);

// Purpose: reads the name of sub key number `index`; false when there is no
// such sub key.
std::uint32_t __fastcall Win32_CRegistryKey_EnumKey(void* self, void* /*edx*/, std::uint32_t index,
                                                    std::uint32_t name, std::uint32_t nameLength,
                                                    std::uint32_t keyClass, std::uint32_t classLength,
                                                    std::uint32_t lastWrite) {
    std::uint32_t written[2] = {0, 0};  // a FILETIME
    const std::int32_t result = g_importRegEnumKeyExA.get()(
        KeyOf(self)->key, index, reinterpret_cast<char*>(name), reinterpret_cast<std::uint32_t*>(nameLength),
        nullptr, reinterpret_cast<char*>(keyClass), reinterpret_cast<std::uint32_t*>(classLength), written);
    std::uint32_t found = 1;
    if (result == kErrorNoMoreItems) {
        found = 0;
    } else {
        Check(result, kLineEnumKey);
    }
    if (lastWrite != 0) {
        std::memcpy(reinterpret_cast<void*>(lastWrite), written, sizeof(written));
    }
    // AL is the flag; the rest of EAX is left from the lastWrite pointer.
    return (lastWrite & 0xFFFFFF00u) | found;
}

S3RE_HOOK(0x006175c0, Win32_CRegistryKey_EnumValue, Member6Fn, Original_Win32_CRegistryKey_EnumValue,
          Win32_CRegistryKey_EnumValue);

// Purpose: reads value number `index` (name, type, data); false when there
// is no such value.
std::uint32_t __fastcall Win32_CRegistryKey_EnumValue(void* self, void* /*edx*/, std::uint32_t index,
                                                      std::uint32_t name, std::uint32_t nameLength,
                                                      std::uint32_t type, std::uint32_t data,
                                                      std::uint32_t dataSize) {
    const std::int32_t result = g_importRegEnumValueA.get()(
        KeyOf(self)->key, index, reinterpret_cast<char*>(name), reinterpret_cast<std::uint32_t*>(nameLength),
        nullptr, reinterpret_cast<std::uint32_t*>(type), reinterpret_cast<void*>(data),
        reinterpret_cast<std::uint32_t*>(dataSize));
    if (result == kErrorNoMoreItems) {
        return static_cast<std::uint32_t>(kErrorNoMoreItems) & 0xFFFFFF00u;  // AL = false over the result
    }
    Check(result, kLineEnumValue);
    return 1;
}

S3RE_HOOK(0x00617810, Win32_CRegistryKey_QueryValue, Member4Fn, Original_Win32_CRegistryKey_QueryValue,
          Win32_CRegistryKey_QueryValue);

// Purpose: reads a value's type and data.
std::uint32_t __fastcall Win32_CRegistryKey_QueryValue(void* self, void* /*edx*/, std::uint32_t name,
                                                       std::uint32_t type, std::uint32_t data,
                                                       std::uint32_t dataSize) {
    const std::int32_t result = g_importRegQueryValueExA.get()(
        KeyOf(self)->key, reinterpret_cast<const char*>(name), nullptr, reinterpret_cast<std::uint32_t*>(type),
        reinterpret_cast<void*>(data), reinterpret_cast<std::uint32_t*>(dataSize));
    return Check(result, kLineQueryValue);
}

S3RE_HOOK(0x00617c80, Win32_CRegistryKey_SetValue, Member4Fn, Original_Win32_CRegistryKey_SetValue,
          Win32_CRegistryKey_SetValue);

// Purpose: writes a value.
std::uint32_t __fastcall Win32_CRegistryKey_SetValue(void* self, void* /*edx*/, std::uint32_t name,
                                                     std::uint32_t type, std::uint32_t data, std::uint32_t size) {
    const std::int32_t result = g_importRegSetValueExA.get()(KeyOf(self)->key, reinterpret_cast<const char*>(name), 0,
                                                             type, reinterpret_cast<const void*>(data), size);
    return Check(result, kLineSetValue);
}

S3RE_HOOK(0x00618030, Win32_CRegistryKey_ValueExists, MemberArgFn, Original_Win32_CRegistryKey_ValueExists,
          Win32_CRegistryKey_ValueExists);

// Purpose: tells whether the key has a value of this name (asked before the
// game reads one of its settings).
std::uint32_t __fastcall Win32_CRegistryKey_ValueExists(void* self, void* /*edx*/, std::uint32_t name) {
    const std::int32_t result = g_importRegQueryValueExA.get()(
        KeyOf(self)->key, reinterpret_cast<const char*>(name), nullptr, nullptr, nullptr, nullptr);
    if (result == 0) {
        return 1;
    }
    if (result != kErrorFileNotFound) {
        ThrowRegistryError(result, kLineValueExists);
    }
    return 0;
}

S3RE_HOOK(0x006171f0, Win32_CRegistryKey_GetSubKeyCount, MemberFn, Original_Win32_CRegistryKey_GetSubKeyCount,
          Win32_CRegistryKey_GetSubKeyCount);

// Purpose: the number of sub keys.
std::uint32_t __fastcall Win32_CRegistryKey_GetSubKeyCount(void* self, void* /*edx*/) {
    std::uint32_t count = 0;  // uninitialized in the original; written by a successful query
    reinterpret_cast<QueryInfoFn>(kRegistryKeyQueryInfo)(self, nullptr, nullptr, nullptr, &count, nullptr, nullptr,
                                                         nullptr, nullptr, nullptr, nullptr, nullptr);
    return count;
}

S3RE_HOOK(0x00617320, Win32_CRegistryKey_GetValueCount, MemberFn, Original_Win32_CRegistryKey_GetValueCount,
          Win32_CRegistryKey_GetValueCount);

// Purpose: the number of values.
std::uint32_t __fastcall Win32_CRegistryKey_GetValueCount(void* self, void* /*edx*/) {
    std::uint32_t count = 0;  // uninitialized in the original; written by a successful query
    reinterpret_cast<QueryInfoFn>(kRegistryKeyQueryInfo)(self, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                                         &count, nullptr, nullptr, nullptr, nullptr);
    return count;
}

S3RE_HOOK(0x00617c40, Win32_CRegistryKey_QueryDword, MemberArgFn, Original_Win32_CRegistryKey_QueryDword,
          Win32_CRegistryKey_QueryDword);

// Purpose: reads a number setting.
std::uint32_t __fastcall Win32_CRegistryKey_QueryDword(void* self, void* /*edx*/, std::uint32_t name) {
    const auto* text = reinterpret_cast<const String*>(name);
    std::uint32_t size = kDwordSize;
    std::uint32_t type;  // not read
    // The value is read into the stack slot of the `name` argument.
    std::uint32_t value = name;
    reinterpret_cast<QueryValueFn>(kRegistryKeyQueryValue)(self, nullptr, CStrOf(text), &type, &value, &size);
    return value;
}

S3RE_HOOK(0x00618000, Win32_CRegistryKey_SetDword, Member2Fn, Original_Win32_CRegistryKey_SetDword,
          Win32_CRegistryKey_SetDword);

// Purpose: writes a number setting.
std::uint32_t __fastcall Win32_CRegistryKey_SetDword(void* self, void* /*edx*/, std::uint32_t name,
                                                     std::uint32_t value) {
    const auto* text = reinterpret_cast<const String*>(name);
    return reinterpret_cast<SetValueFn>(kRegistryKeySetValue)(self, nullptr, CStrOf(text), kRegDword, &value,
                                                              kDwordSize);
}

S3RE_HOOK(0x00616490, Win32_CRegistryKey_Destroy, MemberFn, Original_Win32_CRegistryKey_Destroy,
          Win32_CRegistryKey_Destroy);

// Purpose: destroys a key object (closes the key).
std::uint32_t __fastcall Win32_CRegistryKey_Destroy(void* self, void* /*edx*/) {
    KeyOf(self)->vtable = vtables::kCRegistryKey;
    return reinterpret_cast<CloseFn>(kRegistryKeyClose)(self, nullptr);
}

S3RE_HOOK(0x00616460, Win32_CRegistryKey_Copy, MemberArgFn, Original_Win32_CRegistryKey_Copy,
          Win32_CRegistryKey_Copy);

// Purpose: copies a key object (opens its own handle to the same key).
std::uint32_t __fastcall Win32_CRegistryKey_Copy(void* self, void* /*edx*/, std::uint32_t other) {
    CRegistryKey* const key = KeyOf(self);
    key->key = 0;
    key->vtable = vtables::kCRegistryKey;
    Reopen(self, reinterpret_cast<const void*>(other));
    return reinterpret_cast<std::uint32_t>(self);
}

S3RE_HOOK(0x00618260, Win32_CRegistryKey_Assign, MemberArgFn, Original_Win32_CRegistryKey_Assign,
          Win32_CRegistryKey_Assign);

// Purpose: assigns a key object (closes its key, opens the other's).
std::uint32_t __fastcall Win32_CRegistryKey_Assign(void* self, void* /*edx*/, std::uint32_t other) {
    if (self != reinterpret_cast<void*>(other)) {
        reinterpret_cast<CloseFn>(kRegistryKeyClose)(self, nullptr);
        Reopen(self, reinterpret_cast<const void*>(other));
    }
    return reinterpret_cast<std::uint32_t>(self);
}

using SetStringFn = std::uint32_t(__fastcall*)(void* self, void* edx, std::uint32_t name, String value,
                                               std::uint32_t expand);

S3RE_HOOK(0x00617a40, Win32_CRegistryKey_QueryString, Member2Fn, Original_Win32_CRegistryKey_QueryString,
          Win32_CRegistryKey_QueryString);

// Purpose: reads a text setting.
std::uint32_t __fastcall Win32_CRegistryKey_QueryString(void* self, void* /*edx*/, std::uint32_t result,
                                                        std::uint32_t name) {
    const auto* key = reinterpret_cast<const String*>(name);
    const auto allocator = static_cast<std::uint8_t>(name & 0xFF);  // the stack byte the original uses
    String text;
    framework::MakeEmpty(&text, allocator);
    std::uint32_t size = 0;
    std::uint32_t type;  // not read
    const auto queryValue = reinterpret_cast<QueryValueFn>(kRegistryKeyQueryValue);
    queryValue(self, nullptr, CStrOf(key), &type, nullptr, &size);
    char* const buffer = NewBuffer(size, texts::kRegistryLpData, kLineValueData);
    queryValue(self, nullptr, CStrOf(key), nullptr, buffer, &size);
    // (The original assigns "" when the buffer is null, after the throw.)
    AssignThroughTemporary(&text, buffer, allocator);
    msvc6::GameOperatorDelete(buffer);
    auto* out = reinterpret_cast<String*>(result);
    out->allocator = allocator;
    out->ptr = nullptr;
    out->len = 0;
    out->res = 0;
    msvc6::String_AssignSubstr(out, nullptr, &text, 0, msvc6::detail::Npos());
    framework::EraseAndFree(&text);
    return result;
}

S3RE_HOOK(0x00617eb0, Win32_CRegistryKey_SetString, SetStringFn, Original_Win32_CRegistryKey_SetString,
          Win32_CRegistryKey_SetString);

// Purpose: writes a text setting.
std::uint32_t __fastcall Win32_CRegistryKey_SetString(void* self, void* /*edx*/, std::uint32_t name, String value,
                                                      std::uint32_t expand) {
    const std::uint32_t type = (expand & 0xFF) != 0 ? kRegExpandSz : kRegSz;
    reinterpret_cast<SetValueFn>(kRegistryKeySetValue)(self, nullptr, CStrOf(reinterpret_cast<const String*>(name)),
                                                       type, CStrOf(&value), value.len + 1);
    // The by-value argument dies here (erase, then free).
    framework::EraseAndFree(&value);
    return 0;
}

S3RE_HOOK(0x00617220, Win32_CRegistryKey_GetSubKey, Member2Fn, Original_Win32_CRegistryKey_GetSubKey,
          Win32_CRegistryKey_GetSubKey);

// Purpose: opens sub key number `index` (used to walk all sub keys).
std::uint32_t __fastcall Win32_CRegistryKey_GetSubKey(void* self, void* /*edx*/, std::uint32_t result,
                                                      std::uint32_t index) {
    LocalKey local;
    std::uint32_t length = 0;
    reinterpret_cast<QueryInfoFn>(kRegistryKeyQueryInfo)(self, nullptr, nullptr, nullptr, nullptr, &length, nullptr,
                                                         nullptr, nullptr, nullptr, nullptr, nullptr);
    length += 1;
    char* const name = NewBuffer(length, texts::kRegistryName, kLineSubKeyName);
    reinterpret_cast<EnumKeyFn>(kRegistryKeyEnumKey)(self, nullptr, index, name, &length, nullptr, nullptr, nullptr);
    reinterpret_cast<OpenFn>(kRegistryKeyOpen)(&local.key, nullptr, KeyOf(self)->key, name, kKeyAllAccess);
    msvc6::GameOperatorDelete(name);
    reinterpret_cast<CopyFn>(kRegistryKeyCopy)(reinterpret_cast<void*>(result), nullptr, &local.key);
    return result;  // ~LocalKey destroys the local key
}

S3RE_HOOK(0x00617350, Win32_CRegistryKey_GetValueInfo, Member2Fn, Original_Win32_CRegistryKey_GetValueInfo,
          Win32_CRegistryKey_GetValueInfo);

// Purpose: the name and type of value number `index` (used to walk all
// values).
std::uint32_t __fastcall Win32_CRegistryKey_GetValueInfo(void* self, void* /*edx*/, std::uint32_t result,
                                                         std::uint32_t index) {
    const auto allocator = static_cast<std::uint8_t>(index & 0xFF);  // the stack byte the original uses
    String text;
    framework::MakeEmpty(&text, allocator);
    std::uint32_t longest = 0;
    reinterpret_cast<QueryInfoFn>(kRegistryKeyQueryInfo)(self, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                                         nullptr, &longest, nullptr, nullptr, nullptr);
    std::uint32_t length = longest + 1;
    char* const name = NewBuffer(length, texts::kRegistryLpName, kLineValueName);
    std::uint32_t type = 0;  // uninitialized in the original; written by a successful EnumValue
    reinterpret_cast<EnumValueFn>(kRegistryKeyEnumValue)(self, nullptr, index, name, &length, &type, nullptr,
                                                         nullptr);
    // (The original assigns "" when the buffer is null, after the throw.)
    AssignThroughTemporary(&text, name, allocator);
    msvc6::GameOperatorDelete(name);
    // A local CRegistryValueInfo from a copy of the name (passed by value).
    String copy;
    msvc6::String_FromSubstrDefault(&copy, nullptr, &text, 0, 0xFFFFFFFFu);
    framework::CRegistryValueInfo local;
    reinterpret_cast<ValueInfoConstructFn>(kValueInfoConstruct)(&local, nullptr, copy, type);
    auto* out = reinterpret_cast<framework::CRegistryValueInfo*>(result);
    // The result name's allocator byte is read from the stack slot of
    // `index`, which the original has overwritten by then with the address
    // of the by-value temporary (its entry ESP - 0x64; the same for our
    // frame, since the caller's ESP is the same).
    const auto entry = reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    out->name.allocator = static_cast<std::uint8_t>((entry - 0x64) & 0xFF);
    msvc6::String_Tidy(&out->name, nullptr, 0);
    msvc6::String_AssignSubstr(&out->name, nullptr, &local.name, 0, 0xFFFFFFFFu);
    out->vtable = vtables::kCRegistryValueInfo;
    out->value = local.value;
    reinterpret_cast<CloseFn>(kValueInfoDestroy)(&local, nullptr);
    framework::EraseAndFree(&text);
    return result;
}

S3RE_HOOK(0x00616320, Win32_CRegistryValueInfo_Destroy, MemberFn, Original_Win32_CRegistryValueInfo_Destroy,
          Win32_CRegistryValueInfo_Destroy);

// Purpose: destroys a value description (frees its name).
std::uint32_t __fastcall Win32_CRegistryValueInfo_Destroy(void* self, void* /*edx*/) {
    auto* info = static_cast<framework::CRegistryValueInfo*>(self);
    info->vtable = vtables::kCRegistryValueInfo;
    framework::EraseAndFree(&info->name);
    return 0;
}

}  // namespace s3re::game
