// s3re_launcher: starts the original S3.EXE with s3re.dll loaded into it.
//
// Usage: s3re_launcher [--game-dir <path>] [--test [filter]]
//                      [--trace-determinism] [--log-commands] [--seed <n>]
//                      [--record-commands <file>] [--replay-commands <file>]
//                      [--exit-after-ticks <n>] [--timeout <seconds>]
//                      [--dump-tick <n>] [--load-save <file>]
//                      [-- <arguments passed to S3.EXE>]
//
// --trace-determinism writes the tick, RNG state and world hashes after each
// simulation step to determinism-<date>-<time>.tsv next to s3re.dll.
//
// --seed <n> replaces every seed of the simulation's random number generator
// with n (0..65535), so that two games can start from the same random state.
//
// --dump-tick <n>[,<n>...] (with --trace-determinism) also writes the hashed
// world memory at those ticks to <trace>-world-<n>.bin, for finding which
// bytes differ. --dump-headers appends the world header at every hashed
// tick to <trace>-headers.bin (about 40 MB per 3,000 ticks).
// --original runs the game with the original code: our reimplementations are
// not installed (tooling hooks are). Replay one recording with and without it
// and compare the traces to check our code on whole games.
//
// --unverified also installs the reimplementations that have no verification
// evidence yet (S3RE_HOOK_UNVERIFIED, inventory status `reimplemented`). By
// default only verified ones are installed. Ignored with --original.
//
// --log-commands logs every command the simulation sends to the network
// layer (and, in network games, every command announced to CNetThread) to
// s3re.log.
//
// --record-commands <file> records every command with the step it belongs
// to; --replay-commands <file> replays such a recording instead of the live
// commands (see src/s3re/replay/command_log.h). A recording stores the game
// seed and map, and the replay uses them (an explicit --seed or -map must
// agree); version 1 recordings need --seed and -map.
//
// --exit-after-ticks <n> ends the game once the game tick reaches n, through
// the game's normal shutdown (so `-record` files are completed).
// --timeout <seconds> kills the game if it is still running after that time
// (a safety net for unattended runs; exit code 1).
//
// Everything after `--` is passed to S3.EXE. The game's own options include
// -map <file> (startup map), -demo (skip the menus), -record / -play (record
// or play back the "movie" Siedler3.evt; -play duplicates computer-player
// commands, so use --replay-commands instead) and -help.
//
// --load-save <file> starts the game directly from a savegame (no menus),
// through the game's own load code (see src/s3re/game/savegame_start.cpp).
// <file> is looked up as given, then in <game dir>\Save\. The launcher
// loads a copy (s3re-loaded.sav next to s3re.dll), so the game never opens
// the user's savegame itself. The RNG state comes from the savegame, so
// --seed has no effect; not combinable with -map or the command recorder.
//
// --test runs the differential test suite inside S3.EXE instead of the game
// (optionally only tests whose name contains <filter>), prints the results
// and exits with status 0 only if every test passed.
//
// The game directory is taken from, in order: --game-dir, the S3RE_GAME_DIR
// environment variable, or the default baked in at configure time.

#include "common/recording_format.h"

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

std::wstring FullPath(const std::wstring& path) {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetFullPathNameW(path.c_str(), MAX_PATH, buffer, nullptr);
    return (length > 0 && length < MAX_PATH) ? std::wstring(buffer) : path;
}

// Resolves junctions and symbolic links (e.g. a git worktree's
// Settlers3Ultimate junction to the main checkout). Windows applies the
// compatibility settings for S3.EXE (needed for its display modes) by the
// executable's path, so the game must be started from its real location.
std::wstring FinalPath(const std::wstring& path) {
    HANDLE handle = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return path;
    }
    wchar_t buffer[MAX_PATH * 2];
    const DWORD length = GetFinalPathNameByHandleW(handle, buffer, MAX_PATH * 2,
                                                   FILE_NAME_NORMALIZED);
    CloseHandle(handle);
    if (length == 0 || length >= MAX_PATH * 2) {
        return path;
    }
    std::wstring result(buffer, length);
    if (result.rfind(L"\\\\?\\UNC\\", 0) == 0) {
        result = L"\\\\" + result.substr(8);
    } else if (result.rfind(L"\\\\?\\", 0) == 0) {
        result = result.substr(4);
    }
    return result;
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

std::wstring Widen(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int length = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0);
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), &wide[0], length);
    return wide;
}

// Takes the game's seed and map from a recording (--replay-commands), so a
// replay always starts the game the commands were recorded in. An explicit
// --seed or -map must agree with the recording. Returns 0 or an exit code.
int ApplyRecording(const std::wstring& path, std::wstring& seed,
                   std::vector<std::wstring>& gameArgs) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr) {
        return Fail(L"cannot read %s", path);
    }
    const s3re::recording::Header header = s3re::recording::ReadHeader(file);
    fclose(file);
    if (header.version == 0) {
        return Fail(L"not a command recording: %s", path);
    }
    if (header.version == 1) {
        if (seed.empty()) {
            return Fail(L"%s is a version 1 recording without seed and map: give --seed and "
                        L"-map, or convert it with tools/command_log.py upgrade", path);
        }
        return 0;
    }
    if (header.seed > 0xFFFF) {
        if (seed.empty()) {
            return Fail(L"%s doesn't know its seed: give --seed", path);
        }
    } else if (seed.empty()) {
        seed = std::to_wstring(header.seed);
    } else if (wcstoul(seed.c_str(), nullptr, 0) != header.seed) {
        return Fail(L"--seed differs from the recording's seed %s",
                    std::to_wstring(header.seed));
    }
    const std::wstring map = Widen(header.map);
    for (std::size_t i = 0; i + 1 < gameArgs.size(); ++i) {
        if (_wcsicmp(gameArgs[i].c_str(), L"-map") == 0) {
            if (_wcsicmp(gameArgs[i + 1].c_str(), map.c_str()) != 0) {
                return Fail(L"-map differs from the recording's map %s", map);
            }
            return 0;
        }
    }
    if (!map.empty()) {
        gameArgs.push_back(L"-map");
        gameArgs.push_back(map);
    }
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring gameDir;
    std::vector<std::wstring> gameArgs;
    bool testMode = false;
    bool traceDeterminism = false;
    bool logCommands = false;
    bool dumpHeaders = false;
    bool originalMode = false;
    bool unverifiedMode = false;
    std::wstring recordCommands;
    std::wstring replayCommands;
    std::wstring testFilter;
    std::wstring seed;
    std::wstring exitAfterTicks;
    std::wstring dumpTick;
    std::wstring loadSave;
    DWORD timeoutMs = INFINITE;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--game-dir" && i + 1 < argc) {
            gameDir = argv[++i];
        } else if (arg == L"--test") {
            testMode = true;
            if (i + 1 < argc && argv[i + 1][0] != L'-') {
                testFilter = argv[++i];
            }
        } else if (arg == L"--trace-determinism") {
            traceDeterminism = true;
        } else if (arg == L"--record-commands" && i + 1 < argc) {
            recordCommands = FullPath(argv[++i]);
        } else if (arg == L"--replay-commands" && i + 1 < argc) {
            replayCommands = FullPath(argv[++i]);
            if (!FileExists(replayCommands)) {
                return Fail(L"command recording not found: %s", replayCommands);
            }
        } else if (arg == L"--original") {
            originalMode = true;
        } else if (arg == L"--unverified") {
            unverifiedMode = true;
        } else if (arg == L"--dump-headers") {
            dumpHeaders = true;
        } else if (arg == L"--log-commands") {
            logCommands = true;
        } else if (arg == L"--exit-after-ticks" && i + 1 < argc) {
            exitAfterTicks = argv[++i];
            if (wcstoul(exitAfterTicks.c_str(), nullptr, 10) == 0) {
                return Fail(L"--exit-after-ticks expects a positive number, got '%s'",
                            exitAfterTicks);
            }
        } else if (arg == L"--load-save" && i + 1 < argc) {
            loadSave = argv[++i];
        } else if (arg == L"--dump-tick" && i + 1 < argc) {
            dumpTick = argv[++i];
        } else if (arg == L"--timeout" && i + 1 < argc) {
            const unsigned long seconds = wcstoul(argv[++i], nullptr, 10);
            if (seconds == 0 || seconds > 24 * 3600) {
                return Fail(L"--timeout expects a number of seconds, got '%s'", argv[i]);
            }
            timeoutMs = seconds * 1000;
        } else if (arg == L"--seed" && i + 1 < argc) {
            seed = argv[++i];
            wchar_t* end = nullptr;
            const unsigned long value = wcstoul(seed.c_str(), &end, 0);
            if (end == seed.c_str() || *end != L'\0' || value > 0xFFFF) {
                return Fail(L"--seed expects a number from 0 to 65535, got '%s'", seed);
            }
        } else if (arg == L"--") {
            for (++i; i < argc; ++i) {
                gameArgs.emplace_back(argv[i]);
            }
        } else if (arg == L"--help" || arg == L"-h") {
            wprintf(L"Usage: s3re_launcher [--game-dir <path>] [--test [filter]] "
                    L"[--trace-determinism] [--log-commands] [--record-commands <file>] "
                    L"[--replay-commands <file>] [--seed <n>] [--exit-after-ticks <n>] "
                    L"[--timeout <seconds>] [--dump-tick <n>] [--dump-headers] [--load-save <file>] [--original] [--unverified] [-- <S3.EXE arguments>]\n");
            return 0;
        } else {
            return Fail(L"unknown argument '%s' (try --help)", arg);
        }
    }

    if (!replayCommands.empty()) {
        if (const int error = ApplyRecording(replayCommands, seed, gameArgs)) {
            return error;
        }
    }

    if (gameDir.empty()) {
        wchar_t fromEnv[MAX_PATH];
        const DWORD length = GetEnvironmentVariableW(L"S3RE_GAME_DIR", fromEnv, MAX_PATH);
        gameDir = (length > 0 && length < MAX_PATH) ? fromEnv : S3RE_DEFAULT_GAME_DIR;
    }
    gameDir = FinalPath(gameDir);

    const std::wstring exePath = gameDir + L"\\" + kExeName;
    const std::wstring dllPath = DirectoryOf(ModulePath(nullptr)) + L"\\" + kDllName;

    std::wstring loadSaveCopy;
    if (!loadSave.empty()) {
        if (!recordCommands.empty() || !replayCommands.empty()) {
            return Fail(L"--load-save can't be combined with --record-commands or "
                        L"--replay-commands%s");
        }
        for (const std::wstring& arg : gameArgs) {
            if (_wcsicmp(arg.c_str(), L"-map") == 0) {
                return Fail(L"--load-save can't be combined with -map%s");
            }
        }
        std::wstring source = FullPath(loadSave);
        if (!FileExists(source)) {
            source = FullPath(gameDir + L"\\Save\\" + loadSave);
        }
        if (!FileExists(source)) {
            return Fail(L"savegame not found: %s", loadSave);
        }
        // Load a copy: the game keeps the file open while it runs and could
        // write to its own savegames; the user's files stay untouched.
        loadSaveCopy = DirectoryOf(dllPath) + L"\\s3re-loaded.sav";
        if (!CopyFileW(source.c_str(), loadSaveCopy.c_str(), FALSE)) {
            return Fail(L"could not copy the savegame to %s", loadSaveCopy);
        }
        SetFileAttributesW(loadSaveCopy.c_str(), FILE_ATTRIBUTE_NORMAL);
        wprintf(L"Loading savegame %s (copy: %s)\n", source.c_str(), loadSaveCopy.c_str());
        // -map makes the menu thread start a game at once; the DLL loads the
        // savegame at that point instead of the map.
        gameArgs.push_back(L"-map");
        gameArgs.push_back(loadSaveCopy);
    }

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
    SetEnvironmentVariableW(L"S3RE_TRACE", traceDeterminism ? L"determinism" : nullptr);
    SetEnvironmentVariableW(L"S3RE_LOG_COMMANDS", logCommands ? L"1" : nullptr);
    SetEnvironmentVariableW(L"S3RE_DUMP_HEADERS", dumpHeaders ? L"1" : nullptr);
    SetEnvironmentVariableW(L"S3RE_ORIGINAL", originalMode ? L"1" : nullptr);
    SetEnvironmentVariableW(L"S3RE_UNVERIFIED", unverifiedMode ? L"1" : nullptr);
    SetEnvironmentVariableW(L"S3RE_RECORD_COMMANDS",
                            recordCommands.empty() ? nullptr : recordCommands.c_str());
    SetEnvironmentVariableW(L"S3RE_REPLAY_COMMANDS",
                            replayCommands.empty() ? nullptr : replayCommands.c_str());
    SetEnvironmentVariableW(L"S3RE_SEED", seed.empty() ? nullptr : seed.c_str());
    SetEnvironmentVariableW(L"S3RE_LOAD_SAVE",
                            loadSaveCopy.empty() ? nullptr : loadSaveCopy.c_str());
    SetEnvironmentVariableW(L"S3RE_DUMP_TICK", dumpTick.empty() ? nullptr : dumpTick.c_str());
    SetEnvironmentVariableW(L"S3RE_EXIT_AFTER_TICKS",
                            exitAfterTicks.empty() ? nullptr : exitAfterTicks.c_str());

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};

    wprintf(L"Launching %s%s\n  with %s\n", commandLine.c_str(),
            testMode ? L" (test mode)" : L"", dllPath.c_str());

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

    if (WaitForSingleObject(process.hProcess, timeoutMs) == WAIT_TIMEOUT) {
        fwprintf(stderr, L"s3re_launcher: timeout reached, killing S3.EXE\n");
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
    }
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
