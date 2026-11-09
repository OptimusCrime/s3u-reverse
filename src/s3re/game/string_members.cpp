// Small game-class functions that only copy, set or destroy embedded MSVC 6.0
// std::string members (msvc6/string.h): compiler-generated destructors,
// assignment operators and setters with the string code inlined. The
// function survey filed them under std::string because they call nothing but
// string internals; the classes they belong to are mostly not identified yet
// (the address in each name keeps them apart).
//
// Destructors (void, thiscall): the inlined ~string of the member at the
// given offset (erase(), then _Tidy(true)), which is String_Destroy.
// Setters (void, thiscall, one argument): member = x, i.e. assign(x, 0, npos).
// Assignment operators (thiscall, return this): plain fields copied as
// dwords, then the string member assigned.
// Constructors:
//   0x005bb0b0  probably a wrapper of DirectPlay's DPSESSIONDESC2 (80 bytes,
//               copied as dwords; the session name at +0x30 is a char*), with
//               the name as a string at +0x50 (DirectX/netsession.cpp)
//   0x005b6790  a GUID (16 bytes, default GUID_NULL from 0x65c0b8) followed
//               by two strings at +0x10 and +0x20 from C texts
//   0x006161e0  Win32::CRegistryValueInfo(std::string name, value): vtable
//               0x65d984, name at +4, value at +0x14; the name is passed by
//               value and destroyed by the callee
// Allocator bytes (the first byte of each string, an empty allocator object)
// get whatever byte the original copies there, sometimes the low byte of an
// unrelated stack slot; we copy the same byte.

#include "game/framework_layout.h"
#include "game/raw.h"
#include "hooks.h"
#include "msvc6/string.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::game {

using msvc6::String;
namespace sd = msvc6::detail;

namespace {

using framework::CRegistryValueInfo;
using framework::SessionDesc;

// The string member at `offset` of an object whose class is not identified
// (only that member is known, so it stays a raw offset; see the header).
// The tests also use it for their generic per-offset comparisons.
String* StringAt(void* object, std::uint32_t offset) {
    return &RawAt<String>(object, offset);
}

const String* StringAt(const void* object, std::uint32_t offset) {
    return &RawAt<String>(object, offset);
}

#pragma pack(push, 1)
// Two dwords and a string (0x4499e0).
struct TwoDwordsAndString {
    std::uint32_t first;   // +0x00
    std::uint32_t second;  // +0x04
    String text;           // +0x08
};
static_assert(offsetof(TwoDwordsAndString, text) == 0x08);

// A lobby record (0x5ed590, Dirk/Lobby.cpp).
struct LobbyRecord5ed590 {
    std::uint32_t vtable;     // +0x00 probably the vtable (not copied)
    std::uint32_t fields[4];  // +0x04
    String text;              // +0x14
};
static_assert(offsetof(LobbyRecord5ed590, text) == 0x14);

// A GUID and two names (0x5b6790).
struct GuidNames {
    std::uint32_t guid[4];  // +0x00
    String first;           // +0x10
    String second;          // +0x20
};
static_assert(offsetof(GuidNames, first) == 0x10);
static_assert(offsetof(GuidNames, second) == 0x20);
#pragma pack(pop)

template <typename T>
T* As(void* self) {
    return static_cast<T*>(self);
}

template <typename T>
const T* As(const void* self) {
    return static_cast<const T*>(self);
}

void CopyDwords(void* to, const void* from, std::uint32_t count) {
    auto* t = static_cast<std::uint32_t*>(to);
    const auto* f = static_cast<const std::uint32_t*>(from);
    for (std::uint32_t i = 0; i < count; ++i) {
        t[i] = f[i];
    }
}

void AssignMember(String* member, const String* x) {
    msvc6::String_AssignSubstr(member, nullptr, x, 0, sd::Npos());
}

// member = string(text) through two temporaries, as 0x5b6790 does it:
// string(text), a copy of that, then the assignment.
void AssignTextViaTemporaries(String* member, const char* text) {
    String first{};
    msvc6::String_AssignChars(&first, nullptr, text, sd::Length(text));
    String second{};
    msvc6::String_AssignSubstr(&second, nullptr, &first, 0, 0xFFFFFFFFu);
    msvc6::String_AssignSubstr(member, nullptr, &second, 0, sd::Npos());
    msvc6::String_Tidy(&second, nullptr, 1);
    if (first.ptr != nullptr) {
        sd::Release(first.ptr);
    }
}

}  // namespace

using DestroyFn = void(__fastcall*)(void* self, void* edx);
using SetStringFn = void(__fastcall*)(void* self, void* edx, const String* x);
using AssignFn = void*(__fastcall*)(void* self, void* edx, const void* x);

void __fastcall DestroyStringMember14_411980(void* self, void* edx);
void __fastcall DestroyStringMember08_441740(void* self, void* edx);
void __fastcall DestroyStringMember14_450ae0(void* self, void* edx);
void __fastcall DestroyStringMember14_455170(void* self, void* edx);
void __fastcall DestroyStringMember04_4581a0(void* self, void* edx);
void __fastcall DestroyStringMember04_459440(void* self, void* edx);
void __fastcall DestroyStringMember50_45f060(void* self, void* edx);
void __fastcall DestroyStringMember04_499470(void* self, void* edx);
void __fastcall DestroyStringMember04_49a070(void* self, void* edx);
void __fastcall DestroyStringMembers10And00_5e1b10(void* self, void* edx);
void __fastcall SetStringMember14_412130(void* self, void* edx, const String* x);
void __fastcall SetStringMember10_4181f0(void* self, void* edx, const String* x);
void* __fastcall AssignTwoDwordsAndString_4499e0(void* self, void* edx, const void* x);
void* __fastcall SessionDesc_Assign_5be000(void* self, void* edx, const void* x);
void* __fastcall LobbyRecord_Assign_5ed590(void* self, void* edx, const void* x);
void* __fastcall SessionDesc_Construct_5bb0b0(void* self, void* edx, const void* desc);
void* __fastcall GuidNames_Construct_5b6790(void* self, void* edx, const void* guid, const char* first,
                                            const char* second);
void* __fastcall CRegistryValueInfo_Construct(void* self, void* edx, String name, std::uint32_t value);

// ---------------------------------------------------------------------------
// Destructors
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00411980, DestroyStringMember14_411980, DestroyFn, Original_DestroyStringMember14_411980, DestroyStringMember14_411980);

// Purpose: Destructor of a game object whose only member that needs cleaning
// up is a string at +0x14 (also run by exception cleanup).
void __fastcall DestroyStringMember14_411980(void* self, void* /*edx*/) {
    msvc6::String_Destroy(StringAt(self, 0x14), nullptr);
}

S3RE_HOOK(0x00441740, DestroyStringMember08_441740, DestroyFn, Original_DestroyStringMember08_441740, DestroyStringMember08_441740);

// Purpose: Destructor of a game object whose only member that needs cleaning
// up is a string at +0x08 (also run by exception cleanup).
void __fastcall DestroyStringMember08_441740(void* self, void* /*edx*/) {
    msvc6::String_Destroy(StringAt(self, 0x08), nullptr);
}

S3RE_HOOK(0x00450ae0, DestroyStringMember14_450ae0, DestroyFn, Original_DestroyStringMember14_450ae0, DestroyStringMember14_450ae0);

// Purpose: Destructor of a game object whose only member that needs cleaning
// up is a string at +0x14 (also run by exception cleanup).
void __fastcall DestroyStringMember14_450ae0(void* self, void* /*edx*/) {
    msvc6::String_Destroy(StringAt(self, 0x14), nullptr);
}

S3RE_HOOK(0x00455170, DestroyStringMember14_455170, DestroyFn, Original_DestroyStringMember14_455170, DestroyStringMember14_455170);

// Purpose: Destructor of a game object whose only member that needs cleaning
// up is a string at +0x14 (also run by exception cleanup).
void __fastcall DestroyStringMember14_455170(void* self, void* /*edx*/) {
    msvc6::String_Destroy(StringAt(self, 0x14), nullptr);
}

S3RE_HOOK(0x004581a0, DestroyStringMember04_4581a0, DestroyFn, Original_DestroyStringMember04_4581a0, DestroyStringMember04_4581a0);

// Purpose: Destructor of a game object whose only member that needs cleaning
// up is a string at +0x04 (also run by exception cleanup).
void __fastcall DestroyStringMember04_4581a0(void* self, void* /*edx*/) {
    msvc6::String_Destroy(StringAt(self, 0x04), nullptr);
}

S3RE_HOOK(0x00459440, DestroyStringMember04_459440, DestroyFn, Original_DestroyStringMember04_459440, DestroyStringMember04_459440);

// Purpose: Destructor of a game object whose only member that needs cleaning
// up is a string at +0x04 (also run by exception cleanup).
void __fastcall DestroyStringMember04_459440(void* self, void* /*edx*/) {
    msvc6::String_Destroy(StringAt(self, 0x04), nullptr);
}

S3RE_HOOK(0x0045f060, DestroyStringMember50_45f060, DestroyFn, Original_DestroyStringMember50_45f060, DestroyStringMember50_45f060);

// Purpose: Destructor of a game object whose only member that needs cleaning
// up is a string at +0x50 (also run by exception cleanup).
void __fastcall DestroyStringMember50_45f060(void* self, void* /*edx*/) {
    msvc6::String_Destroy(StringAt(self, 0x50), nullptr);
}

S3RE_HOOK(0x00499470, DestroyStringMember04_499470, DestroyFn, Original_DestroyStringMember04_499470, DestroyStringMember04_499470);

// Purpose: Destructor of a game object whose only member that needs cleaning
// up is a string at +0x04 (also run by exception cleanup).
void __fastcall DestroyStringMember04_499470(void* self, void* /*edx*/) {
    msvc6::String_Destroy(StringAt(self, 0x04), nullptr);
}

S3RE_HOOK(0x0049a070, DestroyStringMember04_49a070, DestroyFn, Original_DestroyStringMember04_49a070, DestroyStringMember04_49a070);

// Purpose: Destructor of a game object whose only member that needs cleaning
// up is a string at +0x04 (also run by exception cleanup).
void __fastcall DestroyStringMember04_49a070(void* self, void* /*edx*/) {
    msvc6::String_Destroy(StringAt(self, 0x04), nullptr);
}

S3RE_HOOK(0x005e1b10, DestroyStringMembers10And00_5e1b10, DestroyFn,
          Original_DestroyStringMembers10And00_5e1b10, DestroyStringMembers10And00_5e1b10);

// Purpose: Destructor of a lobby object with two strings (at +0x10, then +0).
void __fastcall DestroyStringMembers10And00_5e1b10(void* self, void* /*edx*/) {
    msvc6::String_Destroy(StringAt(self, 0x10), nullptr);
    msvc6::String_Destroy(StringAt(self, 0x00), nullptr);
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

S3RE_HOOK(0x00412130, SetStringMember14_412130, SetStringFn, Original_SetStringMember14_412130,
          SetStringMember14_412130);

// Purpose: Sets a UI object's text (string member at +0x14); probably a
// SetText method.
void __fastcall SetStringMember14_412130(void* self, void* /*edx*/, const String* x) {
    msvc6::String_AssignSubstr(StringAt(self, 0x14), nullptr, x, 0, sd::Npos());
}

S3RE_HOOK(0x004181f0, SetStringMember10_4181f0, SetStringFn, Original_SetStringMember10_4181f0,
          SetStringMember10_4181f0);

// Purpose: Sets a UI object's text (string member at +0x10).
void __fastcall SetStringMember10_4181f0(void* self, void* /*edx*/, const String* x) {
    msvc6::String_AssignSubstr(StringAt(self, 0x10), nullptr, x, 0, sd::Npos());
}

// ---------------------------------------------------------------------------
// Assignment operators
// ---------------------------------------------------------------------------

S3RE_HOOK(0x004499e0, AssignTwoDwordsAndString_4499e0, AssignFn,
          Original_AssignTwoDwordsAndString_4499e0, AssignTwoDwordsAndString_4499e0);

// Purpose: Copies an object of two numbers and a string (operator=).
void* __fastcall AssignTwoDwordsAndString_4499e0(void* self, void* /*edx*/, const void* x) {
    TwoDwordsAndString* const to = As<TwoDwordsAndString>(self);
    const TwoDwordsAndString* const from = As<TwoDwordsAndString>(x);
    to->first = from->first;
    to->second = from->second;
    AssignMember(&to->text, &from->text);
    return self;
}

S3RE_HOOK(0x005be000, SessionDesc_Assign_5be000, AssignFn, Original_SessionDesc_Assign_5be000,
          SessionDesc_Assign_5be000);

// Purpose: Copies a network session description (operator=): the 80-byte
// DirectPlay record and the session name string.
void* __fastcall SessionDesc_Assign_5be000(void* self, void* /*edx*/, const void* x) {
    SessionDesc* const to = As<SessionDesc>(self);
    const SessionDesc* const from = As<SessionDesc>(x);
    CopyDwords(to, from, 0x14);  // the whole DPSESSIONDESC2 (+0..+0x50), as dwords
    AssignMember(&to->name, &from->name);
    return self;
}

S3RE_HOOK(0x005ed590, LobbyRecord_Assign_5ed590, AssignFn, Original_LobbyRecord_Assign_5ed590,
          LobbyRecord_Assign_5ed590);

// Purpose: Copies a lobby record (operator=; Dirk/Lobby.cpp): 16 bytes at +4
// and a string at +0x14, nothing when assigned to itself; +0 (probably the
// vtable) stays.
void* __fastcall LobbyRecord_Assign_5ed590(void* self, void* /*edx*/, const void* x) {
    if (self != x) {
        LobbyRecord5ed590* const to = As<LobbyRecord5ed590>(self);
        const LobbyRecord5ed590* const from = As<LobbyRecord5ed590>(x);
        CopyDwords(to->fields, from->fields, 4);
        AssignMember(&to->text, &from->text);
    }
    return self;
}

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

using SessionDescConstructFn = void*(__fastcall*)(void* self, void* edx, const void* desc);

S3RE_HOOK(0x005bb0b0, SessionDesc_Construct_5bb0b0, SessionDescConstructFn,
          Original_SessionDesc_Construct_5bb0b0, SessionDesc_Construct_5bb0b0);

// Purpose: Builds a network session description from a DirectPlay one
// (probably DPSESSIONDESC2): copies the record and keeps its session name as
// a string. Without a record, only the first field is set to 0.
void* __fastcall SessionDesc_Construct_5bb0b0(void* self, void* /*edx*/, const void* desc) {
    const auto low = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(desc));
    SessionDesc* const session = As<SessionDesc>(self);
    String* name = &session->name;
    name->allocator = low;
    sd::Clear(name);
    if (desc == nullptr) {
        session->desc[0] = 0;
        return self;
    }
    CopyDwords(session, desc, 0x14);  // the DPSESSIONDESC2, as dwords
    const char* text = As<SessionDesc>(desc)->sessionName;
    if (text != nullptr) {
        String temporary{};
        temporary.allocator = low;
        msvc6::String_AssignChars(&temporary, nullptr, text, sd::Length(text));
        msvc6::String_AssignSubstr(name, nullptr, &temporary, 0, sd::Npos());
        if (temporary.ptr != nullptr) {
            sd::Release(temporary.ptr);
        }
    } else {
        const char* empty = reinterpret_cast<const char*>(0x007a7868);
        msvc6::String_AssignChars(name, nullptr, empty, sd::Length(empty));
    }
    return self;
}

using GuidNamesConstructFn = void*(__fastcall*)(void* self, void* edx, const void* guid,
                                                const char* first, const char* second);

S3RE_HOOK(0x005b6790, GuidNames_Construct_5b6790, GuidNamesConstructFn,
          Original_GuidNames_Construct_5b6790, GuidNames_Construct_5b6790);

// Purpose: Builds a record of a GUID and two names (probably a network player
// or session id with short and long name): the GUID defaults to GUID_NULL,
// null names stay empty.
void* __fastcall GuidNames_Construct_5b6790(void* self, void* /*edx*/, const void* guid,
                                            const char* first, const char* second) {
    GuidNames* const record = As<GuidNames>(self);
    CopyDwords(record->guid, reinterpret_cast<const void*>(0x0065c0b8), 4);
    String* a = &record->first;
    a->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(first));
    sd::Clear(a);
    if (first != nullptr) {
        AssignTextViaTemporaries(a, first);
    }
    String* b = &record->second;
    // The compiler reused the stack slot of `first` for &b, and takes the
    // allocator byte from there.
    b->allocator = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(b));
    sd::Clear(b);
    if (second != nullptr) {
        AssignTextViaTemporaries(b, second);
    }
    if (guid != nullptr) {
        CopyDwords(record->guid, guid, 4);
    }
    return self;
}

using RegistryValueInfoConstructFn = void*(__fastcall*)(void* self, void* edx, String name,
                                                        std::uint32_t value);

S3RE_HOOK(0x006161e0, CRegistryValueInfo_Construct, RegistryValueInfoConstructFn,
          Original_CRegistryValueInfo_Construct, CRegistryValueInfo_Construct);

// Purpose: Constructor of Win32::CRegistryValueInfo (a registry value's name
// and a number, probably its type or size), used when the game lists
// registry values.
void* __fastcall CRegistryValueInfo_Construct(void* self, void* /*edx*/, String name,
                                              std::uint32_t value) {
    CRegistryValueInfo* const info = As<CRegistryValueInfo>(self);
    String* member = &info->name;
    member->allocator = static_cast<std::uint8_t>(value);
    sd::Clear(member);
    msvc6::String_AssignSubstr(member, nullptr, &name, 0, 0xFFFFFFFFu);
    info->vtable = 0x0065d984;
    info->value = value;
    // ~string of the by-value parameter (its fields are not cleared: the
    // parameter goes away with the stack frame).
    sd::Erase(&name, 0, sd::Npos());
    if (name.ptr != nullptr) {
        sd::Release(name.ptr);
    }
    return self;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kObjectBytes = 0x60;
constexpr std::uint32_t kGuard = 16;

// Random texts for the tests (non-zero characters).
std::vector<std::string> MakeTexts(test::Context& ctx) {
    std::vector<std::string> texts;
    for (int i = 0; i < 16; ++i) {
        std::string text(ctx.rng().Below(i < 3 ? 3 : 70), 'x');
        for (char& c : text) {
            c = static_cast<char>(1 + ctx.rng().Below(255));
        }
        texts.push_back(text);
    }
    return texts;
}

// A side of a comparison: two objects (bytes, with guard bytes) and a loose
// string, all with string members made the same way on both sides.
struct Side {
    std::vector<std::uint8_t> a, b;
    String loose{};
};

// Fills the string at `offset` of both sides with the same text (shared with
// `shareWith` on that side when asked).
void MakeString(test::Context& ctx, String* x, String* y, const std::vector<std::string>& texts,
                const String* shareX, const String* shareY) {
    *x = String{};
    *y = String{};
    const std::uint8_t allocator = static_cast<std::uint8_t>(ctx.rng().Next32());
    x->allocator = y->allocator = allocator;
    if (ctx.rng().Chance(1, 6)) {
        return;  // empty, no buffer
    }
    if (shareX != nullptr && shareX->ptr != nullptr && ctx.rng().Chance(1, 3)) {
        msvc6::String_AssignSubstr(x, nullptr, shareX, 0, 0xFFFFFFFFu);
        msvc6::String_AssignSubstr(y, nullptr, shareY, 0, 0xFFFFFFFFu);
        return;
    }
    const std::string& text = texts[ctx.rng().Below(static_cast<std::uint32_t>(texts.size()))];
    msvc6::String_AssignChars(x, nullptr, text.c_str(), static_cast<std::uint32_t>(text.size()));
    msvc6::String_AssignChars(y, nullptr, text.c_str(), static_cast<std::uint32_t>(text.size()));
    if (x->ptr != nullptr && ctx.rng().Chance(1, 5)) {
        const std::uint8_t count = ctx.rng().Chance(1, 2) ? 0xFF : 0;  // frozen or not
        x->ptr[-1] = y->ptr[-1] = static_cast<char>(count);
    }
}

bool SameString(test::Context& ctx, const String* x, const String* y) {
    bool same = true;
    same &= S3RE_EXPECT_EQ(x->allocator, y->allocator);
    same &= S3RE_EXPECT_EQ(x->ptr == nullptr, y->ptr == nullptr);
    same &= S3RE_EXPECT_EQ(x->len, y->len);
    same &= S3RE_EXPECT_EQ(x->res, y->res);
    if (same && x->ptr != nullptr) {
        same &= S3RE_EXPECT_EQ(static_cast<std::uint8_t>(x->ptr[-1]), static_cast<std::uint8_t>(y->ptr[-1]));
        same &= S3RE_EXPECT_MEM_EQ(x->ptr, y->ptr, x->len + 1);
    }
    return same;
}

// Compares two objects: plain bytes outside the string members must be
// identical, the string members equal (their pointers differ by side), and
// the members' sharing with each other and with `loose` the same.
bool SameObjects(test::Context& ctx, const std::vector<std::uint8_t>& x, const std::vector<std::uint8_t>& y,
                 const std::vector<std::uint32_t>& strings) {
    bool same = true;
    std::vector<bool> isString(x.size(), false);
    for (std::uint32_t offset : strings) {
        for (std::uint32_t i = 0; i < sizeof(String); ++i) {
            isString[offset + i] = true;
        }
        same &= SameString(ctx, StringAt(x.data(), offset), StringAt(y.data(), offset));
    }
    for (std::size_t i = 0; i < x.size() && same; ++i) {
        if (!isString[i]) {
            same &= S3RE_EXPECT_EQ(x[i], y[i]);
        }
    }
    return same;
}

void ReleaseString(String* s) {
    if (s->ptr != nullptr) {
        msvc6::String_TidyFree(s, nullptr);
    }
}

// The functions of one side.
struct Members {
    DestroyFn destroy[10];
    SetStringFn set14, set10;
    AssignFn assign4499e0, assign5be000, assign5ed590;
    SessionDescConstructFn sessionDesc;
    GuidNamesConstructFn guidNames;
    RegistryValueInfoConstructFn registryValueInfo;
};

constexpr std::uint32_t kDestroyOffsets[10] = {0x14, 0x08, 0x14, 0x14, 0x04, 0x04, 0x50, 0x04, 0x04, 0x10};

Members OriginalMembers() {
    return {{Original_DestroyStringMember14_411980, Original_DestroyStringMember08_441740,
             Original_DestroyStringMember14_450ae0, Original_DestroyStringMember14_455170,
             Original_DestroyStringMember04_4581a0, Original_DestroyStringMember04_459440,
             Original_DestroyStringMember50_45f060, Original_DestroyStringMember04_499470,
             Original_DestroyStringMember04_49a070, Original_DestroyStringMembers10And00_5e1b10},
            Original_SetStringMember14_412130,
            Original_SetStringMember10_4181f0,
            Original_AssignTwoDwordsAndString_4499e0,
            Original_SessionDesc_Assign_5be000,
            Original_LobbyRecord_Assign_5ed590,
            Original_SessionDesc_Construct_5bb0b0,
            Original_GuidNames_Construct_5b6790,
            Original_CRegistryValueInfo_Construct};
}

Members OurMembers() {
    return {{&DestroyStringMember14_411980, &DestroyStringMember08_441740, &DestroyStringMember14_450ae0,
             &DestroyStringMember14_455170, &DestroyStringMember04_4581a0, &DestroyStringMember04_459440,
             &DestroyStringMember50_45f060, &DestroyStringMember04_499470, &DestroyStringMember04_49a070,
             &DestroyStringMembers10And00_5e1b10},
            &SetStringMember14_412130,
            &SetStringMember10_4181f0,
            &AssignTwoDwordsAndString_4499e0,
            &SessionDesc_Assign_5be000,
            &LobbyRecord_Assign_5ed590,
            &SessionDesc_Construct_5bb0b0,
            &GuidNames_Construct_5b6790,
            &CRegistryValueInfo_Construct};
}

// A deliberately wrong setter: copies the text instead of sharing the buffer.
void __fastcall WrongSet14(void* self, void* /*edx*/, const String* x) {
    msvc6::String_AssignChars(StringAt(self, 0x14), nullptr, sd::CStr(x), x->len);
}

void CompareMembers(test::Context& ctx, const Members& candidate, int iterations) {
    const Members original = OriginalMembers();
    const std::vector<std::string> texts = MakeTexts(ctx);
    for (int iteration = 0; iteration < iterations; ++iteration) {
        const int op = static_cast<int>(ctx.rng().Below(16));
        // Two objects per side; random plain bytes, strings at the offsets
        // the operation uses.
        std::vector<std::uint8_t> a1(kObjectBytes + kGuard), b1, a2(kObjectBytes + kGuard), b2;
        ctx.rng().Fill(a1.data(), static_cast<std::uint32_t>(a1.size()));
        ctx.rng().Fill(a2.data(), static_cast<std::uint32_t>(a2.size()));
        b1 = a1;
        b2 = a2;
        std::vector<std::uint32_t> strings;
        if (op < 10) {
            strings = {kDestroyOffsets[op]};
            if (op == 9) {
                strings.push_back(0x00);
            }
        } else if (op == 10) {
            strings = {0x14};
        } else if (op == 11) {
            strings = {0x10};
        } else if (op == 12) {
            strings = {0x08};
        } else if (op == 13) {
            strings = {0x50};
        } else if (op == 14) {
            strings = {0x14};
        } else {
            strings = {0x50};  // the constructors build theirs from nothing
        }
        for (std::uint32_t offset : strings) {
            MakeString(ctx, StringAt(a2.data(), offset), StringAt(b2.data(), offset), texts, nullptr, nullptr);
            MakeString(ctx, StringAt(a1.data(), offset), StringAt(b1.data(), offset), texts,
                       StringAt(a2.data(), offset), StringAt(b2.data(), offset));
        }
        bool self = ctx.rng().Chance(1, 6);
        bool same = true;
        if (op < 10) {
            original.destroy[op](a1.data(), nullptr);
            candidate.destroy[op](b1.data(), nullptr);
            for (std::uint32_t offset : strings) {  // nothing to release afterwards
                *StringAt(a1.data(), offset) = *StringAt(b1.data(), offset) = String{};
            }
        } else if (op <= 11) {
            const std::uint32_t offset = strings[0];
            const String* xa = self ? StringAt(a1.data(), offset) : StringAt(a2.data(), offset);
            const String* xb = self ? StringAt(b1.data(), offset) : StringAt(b2.data(), offset);
            if (op == 10) {
                original.set14(a1.data(), nullptr, xa);
                candidate.set14(b1.data(), nullptr, xb);
            } else {
                original.set10(a1.data(), nullptr, xa);
                candidate.set10(b1.data(), nullptr, xb);
            }
        } else if (op <= 14) {
            const AssignFn fa = op == 12 ? original.assign4499e0 : op == 13 ? original.assign5be000
                                                                            : original.assign5ed590;
            const AssignFn fb = op == 12 ? candidate.assign4499e0 : op == 13 ? candidate.assign5be000
                                                                             : candidate.assign5ed590;
            void* ra = fa(a1.data(), nullptr, self ? a1.data() : a2.data());
            void* rb = fb(b1.data(), nullptr, self ? b1.data() : b2.data());
            same &= S3RE_EXPECT(ra == a1.data() && rb == b1.data());
        } else {
            // Constructors: build into fresh objects a1/b1 (their string
            // members start as garbage, as in raw memory).
            for (std::uint32_t offset : strings) {
                ReleaseString(StringAt(a1.data(), offset));
                ReleaseString(StringAt(b1.data(), offset));
            }
            const std::string& t1 = texts[ctx.rng().Below(static_cast<std::uint32_t>(texts.size()))];
            const std::string& t2 = texts[ctx.rng().Below(static_cast<std::uint32_t>(texts.size()))];
            const int which = static_cast<int>(ctx.rng().Below(3));
            if (which == 0) {
                // Session description: an 80-byte record with a name pointer.
                std::vector<std::uint8_t> desc(0x50);
                ctx.rng().Fill(desc.data(), 0x50);
                const char* name = ctx.rng().Chance(1, 4) ? nullptr : t1.c_str();
                As<SessionDesc>(desc.data())->sessionName = name;
                const void* d = ctx.rng().Chance(1, 8) ? nullptr : desc.data();
                // The low byte of the record pointer becomes the allocator byte.
                original.sessionDesc(a1.data(), nullptr, d);
                candidate.sessionDesc(b1.data(), nullptr, d);
                strings = {0x50};
            } else if (which == 1) {
                std::uint8_t guid[16];
                ctx.rng().Fill(guid, 16);
                const void* g = ctx.rng().Chance(1, 3) ? nullptr : guid;
                const char* first = ctx.rng().Chance(1, 4) ? nullptr : t1.c_str();
                const char* second = ctx.rng().Chance(1, 4) ? nullptr : t2.c_str();
                original.guidNames(a1.data(), nullptr, g, first, second);
                candidate.guidNames(b1.data(), nullptr, g, first, second);
                strings = {0x10, 0x20};
                // b's allocator byte is the low byte of its own address,
                // which differs between the two buffers: compare it apart.
                same &= S3RE_EXPECT_EQ(StringAt(a1.data(), 0x20)->allocator,
                                       static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(
                                           StringAt(a1.data(), 0x20))));
                same &= S3RE_EXPECT_EQ(StringAt(b1.data(), 0x20)->allocator,
                                       static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(
                                           StringAt(b1.data(), 0x20))));
                StringAt(a1.data(), 0x20)->allocator = StringAt(b1.data(), 0x20)->allocator = 0;
            } else {
                String na{}, nb{};
                MakeString(ctx, &na, &nb, texts, StringAt(a2.data(), 0x50), StringAt(b2.data(), 0x50));
                const std::uint32_t value = ctx.rng().Next32();
                original.registryValueInfo(a1.data(), nullptr, na, value);
                candidate.registryValueInfo(b1.data(), nullptr, nb, value);
                strings = {0x04};
            }
        }
        same &= SameObjects(ctx, a1, b1, strings);
        same &= SameObjects(ctx, a2, b2, op >= 15 ? std::vector<std::uint32_t>{0x50} : strings);
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  iteration %d: op %d self=%d", iteration, op, self ? 1 : 0);
            return;
        }
        for (std::uint32_t offset : strings) {
            ReleaseString(StringAt(a1.data(), offset));
            ReleaseString(StringAt(b1.data(), offset));
        }
        for (std::uint32_t offset : (op >= 15 ? std::vector<std::uint32_t>{0x50} : strings)) {
            ReleaseString(StringAt(a2.data(), offset));
            ReleaseString(StringAt(b2.data(), offset));
        }
    }
}

}  // namespace

S3RE_TEST(StringMembers_MatchOriginal) {
    CompareMembers(ctx, OurMembers(), 20000);
}

S3RE_TEST(StringMembers_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    Members wrong = OurMembers();
    wrong.set14 = &WrongSet14;
    CompareMembers(probe, wrong, 20000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::game
