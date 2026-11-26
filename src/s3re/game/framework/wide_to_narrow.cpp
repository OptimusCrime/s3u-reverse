// WideToNarrowString (0x0060b6a0; no named source file, between
// protocol.cpp and Synchronize.cpp): a 16-bit std::basic_string (the lobby
// and chat texts, msvc6/wstring.h) converted into a std::string with the
// C runtime's wcstombs.
//
// Compiler-generated __fastcall-like helper: ECX = the result (a string
// constructed here), EDX = the wide string. A buffer of 2 * length + 2
// bytes from the game's operator new; while pos < length: wcstombs(buffer,
// characters + pos, 2 * length + 2) (0x622308), pos += its result (a byte
// count used as a character index: a quirk for multi-byte conversions; -1
// for an invalid character ends the loop by wrapping pos, and the buffer
// is then left unterminated, so the text is whatever the fresh heap block
// held up to a zero byte; an embedded zero
// character makes wcstombs return 0 there and the loop never ends, in the
// original as in ours), and the
// temporary text is *assigned* the buffer's contents through a temporary
// string (each round replaces the previous text). The buffer is freed
// (also when null), the result is constructed as a copy of the text
// (assign(text, 0, npos)), the text's inlined destructor runs (_Split,
// erase(0, npos), free). EAX = the result. The allocator bytes of the
// strings are uninitialized stack bytes in the original; ours are 0 (the
// result's is visible: the in-process test leaves it out).

#include "game/tables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/string.h"
#include "msvc6/wstring.h"

#include <cstdint>

namespace s3re::game {

namespace {

using msvc6::String;
using msvc6::WString;

constexpr std::uintptr_t kWcstombs = 0x00622308;  // the CRT's wcstombs

using WcstombsFn = std::uint32_t(__cdecl*)(char* destination, const std::uint16_t* source, std::uint32_t size);

}  // namespace

String* __fastcall WideToNarrowString(String* result, const WString* wide);
using WideToNarrowFn = decltype(&WideToNarrowString);

S3RE_HOOK(0x0060b6a0, WideToNarrowString, WideToNarrowFn, Original_WideToNarrowString, WideToNarrowString);

// Purpose: turns a wide (16-bit) game string, e.g. a chat line, into an
// ordinary string for drawing.
String* __fastcall WideToNarrowString(String* result, const WString* wide) {
    String text{};
    msvc6::String_Tidy(&text, nullptr, 0);
    auto* const buffer = static_cast<char*>(msvc6::GameOperatorNew(wide->len * 2 + 2));
    if (buffer != nullptr && wide->len > 0) {
        std::uint32_t position = 0;
        do {
            const std::uint16_t* const characters = wide->ptr != nullptr ? wide->ptr : g_wstringNullBuffer.get();
            position += reinterpret_cast<WcstombsFn>(kWcstombs)(buffer, characters + position, wide->len * 2 + 2);
            String converted{};
            msvc6::String_Tidy(&converted, nullptr, 0);
            msvc6::String_AssignChars(&converted, nullptr, buffer, msvc6::detail::Length(buffer));
            msvc6::String_AssignSubstr(&text, nullptr, &converted, 0, msvc6::detail::Npos());
            msvc6::String_Tidy(&converted, nullptr, 1);
        } while (position < wide->len);
    }
    msvc6::GameOperatorDelete(buffer);
    result->allocator = 0;
    result->ptr = nullptr;
    result->len = 0;
    result->res = 0;
    msvc6::String_AssignSubstr(result, nullptr, &text, 0, 0xFFFFFFFFu);
    // The text's inlined destructor: erase(0, npos) (with the split), free.
    msvc6::detail::Erase(&text, 0, msvc6::detail::Npos());
    msvc6::String_Tidy(&text, nullptr, 1);
    return result;
}

}  // namespace s3re::game
