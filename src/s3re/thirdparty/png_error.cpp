// libpng 1.0.2's error handling (pngerror.c), statically linked into S3.EXE
// for PaintLib's PNG decoder (re/picture_code.md). All cdecl.
//
// png_error (0x005a9e20): png_error(png_ptr, message): calls error_fn
//   (+0x40) if set, then the default handler: fprintf(stderr, "libpng error:
//   %s\n") and longjmp(png_ptr->jmpbuf, 1). Never returns (unless error_fn
//   doesn't return either, as PaintLib's throws).
// png_warning (0x005a9e60): png_warning(png_ptr, message): warning_fn (+0x44)
//   if set, else fprintf(stderr, "libpng warning: %s\n") when png_ptr isn't
//   null (warning_fn is read before that check).
// png_format_buffer (0x005a9ef0): (png_ptr, buffer, message): the chunk name
//   (+0x114, four bytes) with non-letters as "[XX]" hex, then ": " and the
//   first 64 bytes of the message (always 64 are copied, whatever its length;
//   byte 63 becomes the terminator), or just a terminator for a null message.
// png_chunk_error (0x005a9ea0) / png_chunk_warning (0x005a9f80): the message
//   prefixed by png_format_buffer into a stack buffer, then the body of
//   png_error / png_warning (inlined in the original; ours shares it).
//   Known difference (docs/todo/known-differences.md): the original's buffer
//   is 0x50 bytes but png_format_buffer writes up to 82 (four non-letter
//   chunk name bytes give 18 + 64), so the last two bytes overwrite the low
//   half of the return address; png_chunk_warning then returns to a wrong
//   address (png_chunk_error longjmps and never returns). Ours has room for
//   82 bytes. Real chunk names are letters, so a valid PNG never gets there.
// png_set_error_fn (0x005a9fe0): (png_ptr, error_ptr, error_fn, warning_fn).

#include "game/globals.h"
#include "game/tables.h"
#include "game/texts.h"
#include "hooks.h"
#include "test/difftest.h"
#include "thirdparty/png_calls.h"
#include "thirdparty/png_struct.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s3re::thirdparty {

using game::g_crtStderr;
using game::g_pngHexDigits;

namespace {

constexpr std::size_t kMessageCopy = 64;              // bytes of the message png_format_buffer copies
constexpr std::size_t kChunkMessageBytes = 18 + 64;   // what png_format_buffer can write (the original has 0x50)

using UserMessageFn = void(__cdecl*)(PngStruct* png, const char* message);

// libpng's isnonalpha(): not a letter, by the 1.0.2 definition (41..122
// without 91..96).
bool IsNonAlpha(std::uint8_t c) {
    return c < 41 || c > 122 || (c > 90 && c < 97);
}

// png_error's body: the user's handler, then the default one (0x5a9e2a..).
void RaiseError(PngStruct* png, const char* message) {
    if (png->error_fn != nullptr) {
        reinterpret_cast<UserMessageFn>(png->error_fn)(png, message);
    }
    calls::At<calls::FprintfFn>(calls::kFprintf)(&g_crtStderr, game::texts::kPngErrorFormat, message);
    calls::At<calls::LongjmpFn>(calls::kLongjmp)(png->jmpbuf, 1);
}

// png_warning's body (0x5a9e64..).
void RaiseWarning(PngStruct* png, const char* message) {
    if (png->warning_fn != nullptr) {
        reinterpret_cast<UserMessageFn>(png->warning_fn)(png, message);
        return;
    }
    if (png != nullptr) {
        calls::At<calls::FprintfFn>(calls::kFprintf)(&g_crtStderr, game::texts::kPngWarningFormat, message);
    }
}

}  // namespace

using PngMessageFn = void(__cdecl*)(PngStruct* png, const char* message);
using PngFormatBufferFn = void(__cdecl*)(PngStruct* png, char* buffer, const char* message);
using PngSetErrorFn = void(__cdecl*)(PngStruct* png, void* errorPtr, void* errorFn, void* warningFn);

void __cdecl png_error(PngStruct* png, const char* message);
void __cdecl png_warning(PngStruct* png, const char* message);
void __cdecl png_format_buffer(PngStruct* png, char* buffer, const char* message);
void __cdecl png_chunk_error(PngStruct* png, const char* message);
void __cdecl png_chunk_warning(PngStruct* png, const char* message);
void __cdecl png_set_error_fn(PngStruct* png, void* errorPtr, void* errorFn, void* warningFn);

S3RE_HOOK(0x005a9e20, png_error, PngMessageFn, Original_png_error, png_error);
S3RE_HOOK(0x005a9e60, png_warning, PngMessageFn, Original_png_warning, png_warning);
S3RE_HOOK(0x005a9ef0, png_format_buffer, PngFormatBufferFn, Original_png_format_buffer, png_format_buffer);
S3RE_HOOK(0x005a9ea0, png_chunk_error, PngMessageFn, Original_png_chunk_error, png_chunk_error);
S3RE_HOOK(0x005a9f80, png_chunk_warning, PngMessageFn, Original_png_chunk_warning, png_chunk_warning);
S3RE_HOOK(0x005a9fe0, png_set_error_fn, PngSetErrorFn, Original_png_set_error_fn, png_set_error_fn);

// Purpose: libpng's fatal error: reports the message and jumps back to the
// decoder's setjmp (PaintLib's handler throws a CTextException first).
void __cdecl png_error(PngStruct* png, const char* message) {
    RaiseError(png, message);
}

// Purpose: libpng's non-fatal warning (PaintLib ignores them).
void __cdecl png_warning(PngStruct* png, const char* message) {
    RaiseWarning(png, message);
}

// Purpose: Builds "<chunk name>: <message>" for the chunk error and warning
// messages.
void __cdecl png_format_buffer(PngStruct* png, char* buffer, const char* message) {
    std::size_t out = 0;
    for (std::size_t in = 0; in < 4; ++in) {
        const std::uint8_t c = png->chunk_name[in];
        if (IsNonAlpha(c)) {
            buffer[out++] = '[';
            buffer[out++] = g_pngHexDigits[(c >> 4) & 0x0f];
            buffer[out++] = g_pngHexDigits[c & 0x0f];
            buffer[out++] = ']';
        } else {
            buffer[out++] = static_cast<char>(c);
        }
    }
    if (message == nullptr) {
        buffer[out] = '\0';
        return;
    }
    buffer[out++] = ':';
    buffer[out++] = ' ';
    // Deliberately reproduced: always 64 bytes, also past a shorter message.
    std::memcpy(buffer + out, message, kMessageCopy);
    buffer[out + kMessageCopy - 1] = '\0';
}

// Purpose: A fatal error about the current chunk.
void __cdecl png_chunk_error(PngStruct* png, const char* message) {
    char buffer[kChunkMessageBytes];
    calls::At<calls::PngFormatBufferFn>(calls::kPngFormatBuffer)(png, buffer, message);
    RaiseError(png, buffer);
}

// Purpose: A warning about the current chunk.
void __cdecl png_chunk_warning(PngStruct* png, const char* message) {
    char buffer[kChunkMessageBytes];
    calls::At<calls::PngFormatBufferFn>(calls::kPngFormatBuffer)(png, buffer, message);
    RaiseWarning(png, buffer);
}

// Purpose: Installs the decoder's own error and warning handlers (PaintLib's
// CPNGDecoder does).
void __cdecl png_set_error_fn(PngStruct* png, void* errorPtr, void* errorFn, void* warningFn) {
    png->error_ptr = errorPtr;
    png->error_fn = errorFn;
    png->warning_fn = warningFn;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
//
// The handlers are test callbacks: the error callback records the message
// and throws (as PaintLib's does), so the default handler's longjmp is not
// reached in-process (the emulator spec covers it, with longjmp probed).

namespace {

struct ErrorThrown {};

std::vector<std::string>* g_messages = nullptr;

void __cdecl RecordAndThrow(PngStruct* /*png*/, const char* message) {
    g_messages->emplace_back(message);
    throw ErrorThrown{};
}

void __cdecl RecordWarning(PngStruct* /*png*/, const char* message) {
    g_messages->emplace_back(message);
}

struct ErrorFunctions {
    PngMessageFn error;
    PngMessageFn warning;
    PngFormatBufferFn format;
    PngMessageFn chunkError;
    PngMessageFn chunkWarning;
    PngSetErrorFn setErrorFn;
};

// Runs one function on a png_struct and returns the messages it reported.
std::vector<std::string> RunOne(const ErrorFunctions& f, int op, PngStruct& png, char* buffer,
                                const char* message, std::uint32_t errorPtr) {
    std::vector<std::string> messages;
    g_messages = &messages;
    try {
        switch (op) {
            case 0: f.error(&png, message); break;
            case 1: f.warning(&png, message); break;
            case 2: f.format(&png, buffer, message); break;
            case 3: f.chunkError(&png, message); break;
            case 4: f.chunkWarning(&png, message); break;
            default:
                f.setErrorFn(&png, reinterpret_cast<void*>(static_cast<std::uintptr_t>(errorPtr)),
                             reinterpret_cast<void*>(&RecordAndThrow), reinterpret_cast<void*>(&RecordWarning));
                break;
        }
    } catch (const ErrorThrown&) {
        messages.emplace_back("<thrown>");
    }
    g_messages = nullptr;
    return messages;
}

void CompareErrors(test::Context& ctx, const ErrorFunctions& candidate, int cases) {
    const ErrorFunctions original{Original_png_error, Original_png_warning, Original_png_format_buffer,
                                  Original_png_chunk_error, Original_png_chunk_warning,
                                  Original_png_set_error_fn};
    static const std::uint8_t kNameBytes[] = {'I', 'H', 'D', 'R', 'z', 'a', 'Z', 'A', ')', '(', '[', '`',
                                              '{', 'z' + 1, 0, 0xff, 0x80, 'a' - 1, 'Z' + 1, 'p'};
    for (int i = 0; i < cases; ++i) {
        const int op = static_cast<int>(ctx.rng().Below(6));
        PngStruct a{};
        ctx.rng().Fill(&a, sizeof(a));
        for (int k = 0; k < 4; ++k) {
            a.chunk_name[k] = ctx.rng().Chance(1, 2) ? kNameBytes[ctx.rng().Below(sizeof(kNameBytes))]
                                                     : static_cast<std::uint8_t>(ctx.rng().Next32());
        }
        if (op == 4) {
            // The original's buffer is 80 bytes, two short: four non-letters
            // overwrite its return address (see the file comment).
            a.chunk_name[ctx.rng().Below(4)] = 'a' + static_cast<std::uint8_t>(ctx.rng().Below(26));
        }
        a.error_fn = reinterpret_cast<void*>(&RecordAndThrow);
        // No warning handler: the default prints to the game's stderr (nowhere).
        a.warning_fn = ctx.rng().Chance(1, 2) ? reinterpret_cast<void*>(&RecordWarning) : nullptr;
        PngStruct b = a;
        char text[100];
        ctx.rng().Fill(text, sizeof(text));
        text[ctx.rng().Below(80)] = '\0';
        const char* message = ctx.rng().Chance(1, 8) && op == 2 ? nullptr : text;
        char bufferA[120], bufferB[120];
        ctx.rng().Fill(bufferA, sizeof(bufferA));
        std::memcpy(bufferB, bufferA, sizeof(bufferA));
        const std::uint32_t errorPtr = ctx.rng().Next32();
        const auto ma = RunOne(original, op, a, bufferA, message, errorPtr);
        const auto mb = RunOne(candidate, op, b, bufferB, message, errorPtr);
        bool same = S3RE_EXPECT(ma == mb);
        same &= S3RE_EXPECT_MEM_EQ(&a, &b, sizeof(a));
        same &= S3RE_EXPECT_MEM_EQ(bufferA, bufferB, sizeof(bufferA));
        if (!same) {
            ctx.Fail(__FILE__, __LINE__, "  case %d, op %d", i, op);
            return;
        }
    }
}

// Wrong: treats '[' (91) as a letter.
void __cdecl WrongFormatBuffer(PngStruct* png, char* buffer, const char* message) {
    png_format_buffer(png, buffer, message);
    for (int k = 0; k < 4; ++k) {
        if (png->chunk_name[k] == '[') {
            buffer[0] ^= 1;  // the output differs whenever '[' is in the name
            return;
        }
    }
}

}  // namespace

S3RE_TEST(PngError_MatchOriginal) {
    CompareErrors(ctx, {&png_error, &png_warning, &png_format_buffer, &png_chunk_error, &png_chunk_warning,
                        &png_set_error_fn},
                  4000);
}

S3RE_TEST(PngError_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareErrors(probe, {&png_error, &png_warning, &WrongFormatBuffer, &png_chunk_error, &png_chunk_warning,
                          &png_set_error_fn},
                  4000);
    S3RE_EXPECT(probe.failures() > 0);
}

}  // namespace s3re::thirdparty
