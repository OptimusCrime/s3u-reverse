#include "test/difftest.h"

#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace s3re::test {
namespace {

struct RegisteredTest {
    const char* name;
    TestFn fn;
    const char* file;
};

std::vector<RegisteredTest>& Registry() {
    static std::vector<RegisteredTest> tests;
    return tests;
}

constexpr int kMaxLoggedFailuresPerTest = 10;

std::uint64_t Fnv1a64(const char* text) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (; *text != '\0'; ++text) {
        hash ^= static_cast<unsigned char>(*text);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

std::uint64_t BaseSeed() {
    char buffer[32];
    const DWORD length = GetEnvironmentVariableA("S3RE_TEST_SEED", buffer, sizeof(buffer));
    if (length > 0 && length < sizeof(buffer)) {
        return std::strtoull(buffer, nullptr, 0);
    }
    return 0;
}

// Runs one test, turning a crash (access violation etc.) in either the
// original or our code into a reported failure instead of killing the run.
// Must not contain objects with destructors because of __try.
// Where the last crash happened (instruction, and for access violations the
// address accessed), logged with the CRASH line.
ULONG_PTR g_crashAt = 0;
ULONG_PTR g_crashAccess = 0;

int RecordCrash(EXCEPTION_POINTERS* pointers) {
    const EXCEPTION_RECORD* record = pointers->ExceptionRecord;
    g_crashAt = reinterpret_cast<ULONG_PTR>(record->ExceptionAddress);
    g_crashAccess = record->NumberParameters >= 2 ? record->ExceptionInformation[1] : 0;
    return EXCEPTION_EXECUTE_HANDLER;
}

DWORD RunGuarded(TestFn fn, Context* ctx) {
    __try {
        fn(*ctx);
        return 0;
    } __except (RecordCrash(GetExceptionInformation())) {
        return GetExceptionCode();
    }
}

const char* BaseName(const char* path) {
    const char* slash = std::strrchr(path, '\\');
    const char* forward = std::strrchr(path, '/');
    if (forward != nullptr && (slash == nullptr || forward > slash)) {
        slash = forward;
    }
    return slash != nullptr ? slash + 1 : path;
}

}  // namespace

std::uint64_t Rng::Next64() {
    std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

std::uint32_t Rng::Below(std::uint32_t bound) {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(Next32()) * bound) >> 32);
}

std::int32_t Rng::Range(std::int32_t low, std::int32_t high) {
    const auto span = static_cast<std::uint64_t>(static_cast<std::int64_t>(high) - low) + 1;
    if (span > 0xffffffffull) {
        return static_cast<std::int32_t>(Next32());
    }
    return low + static_cast<std::int32_t>(Below(static_cast<std::uint32_t>(span)));
}

void Rng::Fill(void* buffer, std::size_t size) {
    auto* bytes = static_cast<unsigned char*>(buffer);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<unsigned char>(Next64() >> 56);
    }
}

Context::Context(const char* testName, std::uint64_t seed, bool quiet)
    : name_(testName), rng_(seed), quiet_(quiet) {}

void Context::Fail(const char* file, int line, const char* format, ...) {
    ++failures_;
    if (quiet_ || failures_ > kMaxLoggedFailuresPerTest) {
        return;
    }
    char message[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    S3RE_LOG("[test]   FAIL %s:%d: %s", BaseName(file), line, message);
}

bool Context::Expect(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        Fail(file, line, "expected %s", expression);
    }
    return condition;
}

bool Context::ExpectMemEq(const void* a, const void* b, std::size_t size, const char* exprA,
                          const char* exprB, const char* file, int line) {
    const auto* bytesA = static_cast<const unsigned char*>(a);
    const auto* bytesB = static_cast<const unsigned char*>(b);
    for (std::size_t i = 0; i < size; ++i) {
        if (bytesA[i] != bytesB[i]) {
            Fail(file, line, "%s and %s differ at byte %zu of %zu (0x%02x != 0x%02x)", exprA,
                 exprB, i, size, bytesA[i], bytesB[i]);
            return false;
        }
    }
    return true;
}

TestRegistrar::TestRegistrar(const char* name, TestFn fn, const char* file) {
    Registry().push_back({name, fn, file});
}

bool IsTestMode() {
    char mode[16];
    const DWORD length = GetEnvironmentVariableA("S3RE_MODE", mode, sizeof(mode));
    return length > 0 && length < sizeof(mode) && std::strcmp(mode, "test") == 0;
}

// A filter is one or more substrings separated by '|'; a test runs if its
// name contains any of them.
bool MatchesFilter(const char* name, const char* filter) {
    if (filter[0] == '\0') {
        return true;
    }
    char part[256];
    const char* p = filter;
    while (true) {
        const char* end = std::strchr(p, '|');
        const std::size_t n = end ? static_cast<std::size_t>(end - p) : std::strlen(p);
        if (n > 0 && n < sizeof(part)) {
            std::memcpy(part, p, n);
            part[n] = '\0';
            if (std::strstr(name, part) != nullptr) {
                return true;
            }
        }
        if (end == nullptr) {
            return false;
        }
        p = end + 1;
    }
}

int RunAll() {
    char filter[256] = {};
    GetEnvironmentVariableA("S3RE_TEST_FILTER", filter, sizeof(filter));
    const std::uint64_t baseSeed = BaseSeed();

    S3RE_LOG("[test] running tests (filter \"%s\", seed %llu)", filter, baseSeed);

    int run = 0;
    int failed = 0;
    for (const RegisteredTest& test : Registry()) {
        if (!MatchesFilter(test.name, filter)) {
            continue;
        }
        ++run;
        Context ctx(test.name, Fnv1a64(test.name) ^ baseSeed);
        const DWORD start = GetTickCount();
        const DWORD exception = RunGuarded(test.fn, &ctx);
        const DWORD elapsed = GetTickCount() - start;

        if (exception != 0) {
            HMODULE module = nullptr;
            char moduleName[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(g_crashAt), &module)) {
                GetModuleFileNameA(module, moduleName, sizeof(moduleName));
            }
            S3RE_LOG("[test] CRASH %s (exception 0x%08lx at %p = %s+0x%lx, address %p) [%s]", test.name, exception,
                     reinterpret_cast<void*>(g_crashAt), BaseName(moduleName),
                     static_cast<unsigned long>(g_crashAt - reinterpret_cast<ULONG_PTR>(module)),
                     reinterpret_cast<void*>(g_crashAccess), BaseName(test.file));
            ++failed;
        } else if (ctx.failures() > 0) {
            S3RE_LOG("[test] FAIL  %s (%d failure(s)) [%s]", test.name, ctx.failures(),
                     BaseName(test.file));
            ++failed;
        } else {
            S3RE_LOG("[test] ok    %s (%lu ms)", test.name, elapsed);
        }
    }

    S3RE_LOG("[test] %d test(s) run, %d passed, %d failed", run, run - failed, failed);
    if (run == 0) {
        S3RE_LOG("[test] no tests matched the filter");
        return 1;
    }
    return failed;
}

}  // namespace s3re::test
