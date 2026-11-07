// s3re_launcher: starts the original S3.EXE with s3re.dll loaded into it.
//
// Usage: s3re_launcher [--game-dir <path>] [--test [filter]]
//                      [-- <arguments passed to S3.EXE>]
//
// --test runs the differential test suite inside S3.EXE instead of the game
// (optionally only tests whose name contains <filter>), prints the results
// and exits with status 0 only if every test passed.
//
// The game directory is taken from, in order: --game-dir, the S3RE_GAME_DIR
// environment variable, or the default baked in at configure time.

#include <windows.h>
#include <bcrypt.h>
#include <detours.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// SHA-256 of the GOG S3.EXE this project reverses (see CLAUDE.md).
constexpr const char* kExpectedExeSha256 =
    "00a925cfdcfc3349b5d0f3b8a4c312f01b813e363b4464f6f3d6de17804d950b";

constexpr const wchar_t* kExeName = L"S3.EXE";
constexpr const wchar_t* kDllName = L"s3re.dll";

std::wstring DirectoryOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring ModulePath(HMODULE module) {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(module, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Returns the lowercase hex SHA-256 of a file, or an empty string on failure.
std::string Sha256File(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }

    std::string result;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                                   nullptr, 0)) &&
        BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0))) {
        std::vector<unsigned char> chunk(1 << 20);
        DWORD read = 0;
        bool ok = true;
        while (ReadFile(file, chunk.data(), static_cast<DWORD>(chunk.size()), &read,
                        nullptr) &&
               read > 0) {
            if (!BCRYPT_SUCCESS(BCryptHashData(hash, chunk.data(), read, 0))) {
                ok = false;
                break;
            }
        }
        unsigned char digest[32];
        if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
            static const char kHex[] = "0123456789abcdef";
            for (unsigned char byte : digest) {
                result.push_back(kHex[byte >> 4]);
                result.push_back(kHex[byte & 0xF]);
            }
        }
    }
    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    if (algorithm != nullptr) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    CloseHandle(file);
    return result;
}

std::wstring QuoteArgument(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
        } else {
            quoted.append(backslashes, L'\\');
        }
        backslashes = 0;
        quoted.push_back(c);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

// Prints the test lines the DLL wrote to s3re.log.
void PrintTestResults(const std::wstring& logPath) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, logPath.c_str(), L"rb") != 0 || file == nullptr) {
        fwprintf(stderr, L"s3re_launcher: could not read %s\n", logPath.c_str());
        return;
    }
    char line[2048];
    while (fgets(line, sizeof(line), file) != nullptr) {
        if (const char* tag = strstr(line, "[test]")) {
            fputs(tag, stdout);
        }
    }
    fclose(file);
}

int Fail(const wchar_t* format, const std::wstring& detail = {}) {
    fwprintf(stderr, L"s3re_launcher: ");
    fwprintf(stderr, format, detail.c_str());
    fwprintf(stderr, L"\n");
    return 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring gameDir;
    std::vector<std::wstring> gameArgs;
    bool testMode = false;
    std::wstring testFilter;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--game-dir" && i + 1 < argc) {
            gameDir = argv[++i];
        } else if (arg == L"--test") {
            testMode = true;
            if (i + 1 < argc && argv[i + 1][0] != L'-') {
                testFilter = argv[++i];
            }
        } else if (arg == L"--") {
            for (++i; i < argc; ++i) {
                gameArgs.emplace_back(argv[i]);
            }
        } else if (arg == L"--help" || arg == L"-h") {
            wprintf(L"Usage: s3re_launcher [--game-dir <path>] [--test [filter]] "
                    L"[-- <S3.EXE arguments>]\n");
            return 0;
        } else {
            return Fail(L"unknown argument '%s' (try --help)", arg);
        }
    }

    if (gameDir.empty()) {
        wchar_t fromEnv[MAX_PATH];
        const DWORD length = GetEnvironmentVariableW(L"S3RE_GAME_DIR", fromEnv, MAX_PATH);
        gameDir = (length > 0 && length < MAX_PATH) ? fromEnv : S3RE_DEFAULT_GAME_DIR;
    }

    const std::wstring exePath = gameDir + L"\\" + kExeName;
    const std::wstring dllPath = DirectoryOf(ModulePath(nullptr)) + L"\\" + kDllName;

    if (!FileExists(exePath)) {
        return Fail(L"game executable not found: %s", exePath);
    }
    if (!FileExists(dllPath)) {
        return Fail(L"hook DLL not found next to the launcher: %s", dllPath);
    }

    const std::string actualHash = Sha256File(exePath);
    if (actualHash != kExpectedExeSha256) {
        fwprintf(stderr, L"s3re_launcher: %s is not the expected build.\n", exePath.c_str());
        fwprintf(stderr, L"  expected SHA-256: %hs\n", kExpectedExeSha256);
        fwprintf(stderr, L"  actual SHA-256:   %hs\n",
                 actualHash.empty() ? "(could not read file)" : actualHash.c_str());
        return 1;
    }

    // Detours takes the DLL path as an ANSI string.
    char dllPathAnsi[MAX_PATH * 2];
    if (WideCharToMultiByte(CP_ACP, 0, dllPath.c_str(), -1, dllPathAnsi,
                            sizeof(dllPathAnsi), nullptr, nullptr) == 0) {
        return Fail(L"DLL path cannot be represented in the ANSI code page: %s", dllPath);
    }

    std::wstring commandLine = QuoteArgument(exePath);
    for (const std::wstring& arg : gameArgs) {
        commandLine += L" " + QuoteArgument(arg);
    }

    // The game process inherits these; the DLL reads them.
    SetEnvironmentVariableW(L"S3RE_MODE", testMode ? L"test" : nullptr);
    SetEnvironmentVariableW(L"S3RE_TEST_FILTER", testFilter.empty() ? nullptr
                                                                    : testFilter.c_str());

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};

    wprintf(L"Launching %s%s\n  with %s\n", exePath.c_str(), testMode ? L" (test mode)" : L"",
            dllPath.c_str());

    if (!DetourCreateProcessWithDllExW(exePath.c_str(), commandLine.data(), nullptr, nullptr,
                                       FALSE, CREATE_DEFAULT_ERROR_MODE, nullptr,
                                       gameDir.c_str(), &startup, &process, dllPathAnsi,
                                       nullptr)) {
        const DWORD error = GetLastError();
        if (error == ERROR_ELEVATION_REQUIRED) {
            return Fail(L"S3.EXE requires administrator rights; run the launcher from an "
                        L"elevated terminal.%s");
        }
        fwprintf(stderr, L"s3re_launcher: failed to start the game (error %lu)\n", error);
        return 1;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (testMode) {
        PrintTestResults(DirectoryOf(dllPath) + L"\\s3re.log");
        return exitCode == 0 ? 0 : 1;
    }

    wprintf(L"S3.EXE exited with code %lu\n", exitCode);
    return static_cast<int>(exitCode);
}
