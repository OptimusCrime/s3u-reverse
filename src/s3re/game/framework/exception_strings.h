#pragma once

// The string idioms the framework's exception code (Win32\Classes.cpp)
// inlines again and again, as small helpers (session 58,
// wip/framework-errors). Each calls the MSVC 6.0 string members in the
// original's order, so allocations, shared buffers and count bytes come out
// the same. The older files (win32_errors.cpp, directx_sound_files.cpp)
// still have their own copies of some of these.

#include "msvc6/string.h"

#include <cstdint>

namespace s3re::game::framework {

using msvc6::String;

// The empty string the compiler builds in place: the allocator byte, then
// _Tidy(false).
inline void MakeEmpty(String* s, std::uint8_t allocator) {
    s->allocator = allocator;
    msvc6::String_Tidy(s, nullptr, 0);
}

// `target` = string(text) through two constructed temporaries:
// string(text, al) (0x407a50), string(that, 0, -1, al) (0x4078a0),
// target.assign(copy, 0, npos) (0x401d60), then both freed.
inline void AssignThroughConstructors(String* target, const char* text, const std::uint8_t* allocator) {
    String fromText;
    String copy;
    msvc6::String_FromText(&fromText, nullptr, text, allocator);
    msvc6::String_FromSubstr(&copy, nullptr, &fromText, 0, 0xFFFFFFFFu, allocator);
    msvc6::String_AssignSubstr(target, nullptr, &copy, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&copy, nullptr, 1);
    msvc6::String_Tidy(&fromText, nullptr, 1);
}

// The same with the temporaries built by _Tidy + assign: assign(text, n)
// (0x4021c0) and assign(that, 0, -1) (0x401d60).
inline void AssignThroughAssigns(String* target, const char* text, std::uint8_t allocator) {
    String fromText;
    MakeEmpty(&fromText, allocator);
    msvc6::String_AssignChars(&fromText, nullptr, text, msvc6::detail::Length(text));
    String copy;
    MakeEmpty(&copy, allocator);
    msvc6::String_AssignSubstr(&copy, nullptr, &fromText, 0, 0xFFFFFFFFu);
    msvc6::String_AssignSubstr(target, nullptr, &copy, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&copy, nullptr, 1);
    msvc6::String_Tidy(&fromText, nullptr, 1);
}

// A string constructed empty (0x407880), then given `text` as above (the
// `string s = "..."` of the exception code).
inline void ConstructFromText(String* s, const char* text, const std::uint8_t* allocator) {
    msvc6::String_Default(s, nullptr, allocator);
    AssignThroughConstructors(s, text, allocator);
}

// The inlined "erase(0, npos), then free" of a string going out of scope
// (erase splits a shared buffer first).
inline void EraseAndFree(String* s) {
    msvc6::detail::Erase(s, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(s, nullptr, 1);
}

// The same with the out-of-line erase (0x407b10).
inline void CallEraseAndFree(String* s) {
    msvc6::String_Erase(s, nullptr, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(s, nullptr, 1);
}

}  // namespace s3re::game::framework
