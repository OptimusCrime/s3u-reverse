#pragma once

// Differential test harness.
//
// Tests run inside the real S3.EXE process (start it with
// `s3re_launcher --test [filter]`). Hooks are installed as usual, so the
// original code is reached through the trampoline pointer declared by
// S3RE_HOOK (e.g. Original_String_Eos) and our version is called directly.
// A typical test feeds both the same random inputs, from identical starting
// states, and compares return values and every byte of memory they touch.
//
//   S3RE_TEST(String_Eos_MatchesOriginal) {
//       for (int i = 0; i < 10000; ++i) {
//           uint32_t n = ctx.rng().Below(64);
//           ...
//           S3RE_EXPECT_EQ(originalResult, ourResult);
//       }
//   }
//
// Inputs are generated from a seed derived from the test name, so a failure
// reproduces on every run. Set S3RE_TEST_SEED to try other inputs.

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace s3re::test {

// SplitMix64: small, fast and fully deterministic across platforms.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}

    std::uint64_t Next64();
    std::uint32_t Next32() { return static_cast<std::uint32_t>(Next64() >> 32); }
    // Uniform in [0, bound). bound must be > 0.
    std::uint32_t Below(std::uint32_t bound);
    // Uniform in [low, high], inclusive.
    std::int32_t Range(std::int32_t low, std::int32_t high);
    bool Chance(std::uint32_t numerator, std::uint32_t denominator) {
        return Below(denominator) < numerator;
    }
    void Fill(void* buffer, std::size_t size);

private:
    std::uint64_t state_;
};

class Context {
public:
    // A quiet context records failures without logging them (used to test
    // the harness itself).
    Context(const char* testName, std::uint64_t seed, bool quiet = false);

    Rng& rng() { return rng_; }
    int failures() const { return failures_; }
    const char* name() const { return name_; }

    // Records a failure. printf-style message.
    void Fail(const char* file, int line, const char* format, ...);

    bool Expect(bool condition, const char* expression, const char* file, int line);

    template <typename T>
    bool ExpectEq(const T& a, const T& b, const char* exprA, const char* exprB,
                  const char* file, int line) {
        if (a == b) {
            return true;
        }
        if constexpr (std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>) {
            Fail(file, line, "%s != %s (0x%llx != 0x%llx)", exprA, exprB, ToBits(a),
                 ToBits(b));
        } else {
            Fail(file, line, "%s != %s", exprA, exprB);
        }
        return false;
    }

    // Byte-wise comparison; reports the first differing offset.
    bool ExpectMemEq(const void* a, const void* b, std::size_t size, const char* exprA,
                     const char* exprB, const char* file, int line);

private:
    template <typename T>
    static unsigned long long ToBits(const T& value) {
        if constexpr (std::is_pointer_v<T>) {
            return reinterpret_cast<std::uintptr_t>(value);
        } else {
            return static_cast<unsigned long long>(value);
        }
    }

    const char* name_;
    Rng rng_;
    bool quiet_;
    int failures_ = 0;
};

using TestFn = void (*)(Context&);

struct TestRegistrar {
    TestRegistrar(const char* name, TestFn fn, const char* file);
};

// True when the launcher started the game with --test.
bool IsTestMode();

// Runs all registered tests (or those whose name contains S3RE_TEST_FILTER).
// Returns the number of failed tests.
int RunAll();

}  // namespace s3re::test

#define S3RE_TEST(name)                                                            \
    static void s3re_test_##name(::s3re::test::Context& ctx);                       \
    static ::s3re::test::TestRegistrar s3re_test_registrar_##name{#name,             \
                                                                  &s3re_test_##name, \
                                                                  __FILE__};         \
    static void s3re_test_##name([[maybe_unused]] ::s3re::test::Context& ctx)

#define S3RE_EXPECT(condition) ctx.Expect((condition), #condition, __FILE__, __LINE__)

#define S3RE_EXPECT_EQ(a, b) ctx.ExpectEq((a), (b), #a, #b, __FILE__, __LINE__)

#define S3RE_EXPECT_MEM_EQ(a, b, size)                                             \
    ctx.ExpectMemEq((a), (b), (size), #a, #b, __FILE__, __LINE__)
