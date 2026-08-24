#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <cwctype>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    static_assert(sizeof(void*) == 4, "The launcher must match Fable Anniversary's x86 process.");

    constexpr wchar_t kGameExecutableName[] = L"Fable Anniversary.exe";
    constexpr wchar_t kClientDllName[] = L"FableTogether.Client.dll";
    constexpr wchar_t kClientModeEnvironment[] = L"FABLETOGETHER_CLIENT_MODE";
    constexpr wchar_t kScenarioEnvironment[] = L"FABLETOGETHER_SCENARIO";
    constexpr wchar_t kRunIdEnvironment[] = L"FABLETOGETHER_RUN_ID";
    constexpr wchar_t kEventPathEnvironment[] = L"FABLETOGETHER_EVENT_PATH";
    constexpr wchar_t kLogPathEnvironment[] = L"FABLETOGETHER_LOG_PATH";
    constexpr wchar_t kFixtureDocumentsEnvironment[] = L"FABLETOGETHER_FIXTURE_DOCUMENTS";
    constexpr wchar_t kCharacterSnapshotEnvironment[] = L"FABLETOGETHER_CHARACTER_SNAPSHOT";
    constexpr wchar_t kScriptDataEnvironment[] = L"FABLETOGETHER_SCRIPT_DATA";
    constexpr wchar_t kLocalSessionEnvironment[] = L"FABLETOGETHER_LOCAL_SESSION";
    constexpr wchar_t kLocalInstanceEnvironment[] = L"FABLETOGETHER_LOCAL_INSTANCE";
    constexpr wchar_t kMultiplayerRoleEnvironment[] = L"FABLETOGETHER_MULTIPLAYER_ROLE";
    constexpr wchar_t kMultiplayerAddressEnvironment[] = L"FABLETOGETHER_MULTIPLAYER_ADDRESS";
    constexpr wchar_t kMultiplayerPortEnvironment[] = L"FABLETOGETHER_MULTIPLAYER_PORT";
    constexpr wchar_t kMultiplayerPlayerIdEnvironment[] = L"FABLETOGETHER_MULTIPLAYER_PLAYER_ID";
    constexpr wchar_t kMultiplayerAppearanceEnvironment[] = L"FABLETOGETHER_MULTIPLAYER_APPEARANCE";
    constexpr wchar_t kGameDefinitionsEnvironment[] = L"FABLETOGETHER_GAME_DEFINITIONS";
    constexpr wchar_t kManualPlaytestEnvironment[] =
        L"FABLETOGETHER_MANUAL_PLAYTEST";
    constexpr wchar_t kHeroWillPillarOnlyEnvironment[] =
        L"FABLE_TOGETHER_HERO_WILL_PILLAR_ONLY";
    // The deployed definitions sidecar patches this ordinary creature with
    // the Hero graphic and presentation stack. Retaining its creature
    // lifecycle avoids constructing a second world-unique CREATURE_HERO.
    constexpr wchar_t kRemoteHeroDefinition[] =
        L"CREATURE_HERO_RIVAL_GOOD_01";
    constexpr wchar_t kShutdownEventPrefix[] = L"Local\\FableTogether.Shutdown.";
    constexpr wchar_t kDevelopmentGameRoot[] = L"D:\\SteamLibrary\\steamapps\\common\\Fable Anniversary";
    constexpr wchar_t kFableSteamAppId[] = L"288470";
    constexpr DWORD kInjectionTimeoutMilliseconds = 15'000;
    constexpr DWORD kRuntimeReadyTimeoutMilliseconds = 90'000;
    constexpr DWORD kClientPreResumeReady = 0x0000F101;
    constexpr DWORD kClientRuntimeReady = 0x0000F102;
    // Keep two local acceptance peers visible on a 1700px-wide desktop. The
    // game still renders its 1280x720 fixture internally; only the test window
    // frame is compacted after creation.
    constexpr int kLocalTestWindowWidth = 830;
    constexpr int kLocalTestWindowHeight = 620;
    constexpr int kLocalTestWindowPitch = 850;

    class UniqueHandle
    {
    public:
        UniqueHandle() = default;
        explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
        ~UniqueHandle()
        {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle_);
            }
        }

        UniqueHandle(const UniqueHandle&) = delete;
        UniqueHandle& operator=(const UniqueHandle&) = delete;

        UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_)
        {
            other.handle_ = nullptr;
        }

        UniqueHandle& operator=(UniqueHandle&& other) noexcept
        {
            if (this != &other)
            {
                if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(handle_);
                }
                handle_ = other.handle_;
                other.handle_ = nullptr;
            }
            return *this;
        }

        [[nodiscard]] HANDLE get() const { return handle_; }
        [[nodiscard]] bool valid() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

    private:
        HANDLE handle_ = nullptr;
    };

    class ScopedSyntheticKey
    {
    public:
        explicit ScopedSyntheticKey(UINT virtualKey)
            : scanCode_(static_cast<WORD>(
                  MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC)))
        {
        }

        ~ScopedSyntheticKey()
        {
            Release();
        }

        ScopedSyntheticKey(const ScopedSyntheticKey&) = delete;
        ScopedSyntheticKey& operator=(const ScopedSyntheticKey&) = delete;

        bool Press()
        {
            if (down_)
            {
                return true;
            }
            INPUT input = {};
            input.type = INPUT_KEYBOARD;
            input.ki.wScan = scanCode_;
            input.ki.dwFlags = KEYEVENTF_SCANCODE;
            down_ = SendInput(1, &input, sizeof(input)) == 1;
            return down_;
        }

        bool Release()
        {
            if (!down_)
            {
                return true;
            }
            INPUT input = {};
            input.type = INPUT_KEYBOARD;
            input.ki.wScan = scanCode_;
            input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
            const bool released = SendInput(1, &input, sizeof(input)) == 1;
            if (released)
            {
                down_ = false;
            }
            return released;
        }

        [[nodiscard]] bool down() const { return down_; }

    private:
        WORD scanCode_ = 0;
        bool down_ = false;
    };

    // Automation-only stimulus. The injected client does not inspect mouse
    // state; this asks Fable's game-window input path to produce its mapped
    // ATTACK event so the native combat hook can be verified end to end.
    class ScopedSyntheticMouseButton
    {
    public:
        ~ScopedSyntheticMouseButton()
        {
            Release();
        }

        ScopedSyntheticMouseButton(const ScopedSyntheticMouseButton&) = delete;
        ScopedSyntheticMouseButton& operator=(const ScopedSyntheticMouseButton&) = delete;

        ScopedSyntheticMouseButton() = default;

        bool Press(HWND targetWindow)
        {
            if (down_)
            {
                return true;
            }
            if (targetWindow == nullptr)
            {
                return false;
            }
            RECT bounds = {};
            if (!GetClientRect(targetWindow, &bounds))
            {
                return false;
            }
            const int x = (bounds.right - bounds.left) / 2;
            const int y = (bounds.bottom - bounds.top) / 2;
            point_ = MAKELPARAM(x, y);
            window_ = targetWindow;
            down_ = PostMessageW(
                window_,
                WM_LBUTTONDOWN,
                MK_LBUTTON,
                point_) != FALSE;
            return down_;
        }

        bool Release()
        {
            if (!down_)
            {
                return true;
            }
            const bool released = window_ != nullptr &&
                PostMessageW(window_, WM_LBUTTONUP, 0, point_) != FALSE;
            if (released)
            {
                down_ = false;
                window_ = nullptr;
            }
            return released;
        }

        [[nodiscard]] bool down() const { return down_; }

    private:
        HWND window_ = nullptr;
        LPARAM point_ = 0;
        bool down_ = false;
    };

    class ScopedEnvironmentVariable
    {
    public:
        ScopedEnvironmentVariable(const wchar_t* name, const wchar_t* value)
            : name_(name)
        {
            SetLastError(ERROR_SUCCESS);
            const DWORD required = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
            if (required != 0)
            {
                previousValue_.resize(required);
                const DWORD length = GetEnvironmentVariableW(
                    name_.c_str(), previousValue_.data(), required);
                previousValue_.resize(length);
                hadPreviousValue_ = true;
            }
            else if (GetLastError() != ERROR_ENVVAR_NOT_FOUND)
            {
                return;
            }

            applied_ = SetEnvironmentVariableW(name_.c_str(), value) != FALSE;
        }

        ~ScopedEnvironmentVariable()
        {
            if (!applied_)
            {
                return;
            }
            SetEnvironmentVariableW(
                name_.c_str(),
                hadPreviousValue_ ? previousValue_.c_str() : nullptr);
        }

        ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
        ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

        [[nodiscard]] bool applied() const { return applied_; }

    private:
        std::wstring name_;
        std::wstring previousValue_;
        bool hadPreviousValue_ = false;
        bool applied_ = false;
    };

    struct Options
    {
        fs::path executable;
        fs::path gameDirectory;
        fs::path clientDll;
        fs::path fixtureDocuments;
        fs::path characterSnapshot;
        std::vector<std::wstring> gameArguments;
        std::wstring automationScenario;
        std::wstring localSession;
        std::wstring localInstance;
        std::wstring multiplayerRole;
        std::wstring multiplayerAddress;
        std::wstring multiplayerPlayerId;
        std::wstring multiplayerAppearance;
        unsigned short multiplayerPort = 38171;
        unsigned int automationTimeoutSeconds = 120;
        unsigned int dualInstanceHoldSeconds = 10;
        bool transformationProbe = false;
        bool dualInstanceTest = false;
        bool multiplayerTest = false;
        bool multiplayerRosterTest = false;
        bool multiplayerTransitionTest = false;
        bool multiplayerAuthorityTest = false;
        bool multiplayerCombatTest = false;
        bool multiplayerHeroWillTest = false;
        bool multiplayerPlaytest = false;
        bool dryRun = false;
        bool showHelp = false;
    };

    std::wstring FormatWindowsError(DWORD error)
    {
        wchar_t* rawMessage = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error,
            0,
            reinterpret_cast<wchar_t*>(&rawMessage),
            0,
            nullptr);

        std::wstring message = length != 0 && rawMessage != nullptr
            ? std::wstring(rawMessage, length)
            : L"Unknown Windows error";
        if (rawMessage != nullptr)
        {
            LocalFree(rawMessage);
        }
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
        {
            message.pop_back();
        }
        return message;
    }

    fs::path GetLauncherDirectory()
    {
        std::wstring buffer(32'768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
        {
            return {};
        }
        buffer.resize(length);
        return fs::path(buffer).parent_path();
    }

    fs::path AbsolutePath(const fs::path& path)
    {
        std::error_code error;
        fs::path absolute = fs::absolute(path, error);
        return error ? path : absolute.lexically_normal();
    }

    bool IsFile(const fs::path& path)
    {
        std::error_code error;
        return fs::is_regular_file(path, error);
    }

    bool IsDirectory(const fs::path& path)
    {
        std::error_code error;
        return fs::is_directory(path, error);
    }

    bool IsSamePathOrBelow(const fs::path& candidate, const fs::path& root)
    {
        const fs::path normalizedCandidate = AbsolutePath(candidate);
        const fs::path normalizedRoot = AbsolutePath(root);
        auto candidatePart = normalizedCandidate.begin();
        for (auto rootPart = normalizedRoot.begin();
             rootPart != normalizedRoot.end();
             ++rootPart, ++candidatePart)
        {
            if (candidatePart == normalizedCandidate.end() ||
                _wcsicmp(
                    candidatePart->c_str(),
                    rootPart->c_str()) != 0)
            {
                return false;
            }
        }
        return true;
    }

    fs::path GetOrdinaryDocumentsPath()
    {
        std::wstring userProfile(32'768, L'\0');
        const DWORD length = GetEnvironmentVariableW(
            L"USERPROFILE",
            userProfile.data(),
            static_cast<DWORD>(userProfile.size()));
        if (length == 0 || length >= userProfile.size())
        {
            return {};
        }
        userProfile.resize(length);
        return AbsolutePath(fs::path(userProfile) / L"Documents");
    }

    fs::path ExecutableBelow(const fs::path& directory)
    {
        const fs::path direct = directory / kGameExecutableName;
        if (IsFile(direct))
        {
            return AbsolutePath(direct);
        }

        const fs::path conventional = directory / L"Binaries" / L"Win32" / kGameExecutableName;
        if (IsFile(conventional))
        {
            return AbsolutePath(conventional);
        }

        return {};
    }

    fs::path ResolveDeploymentAsset(
        const fs::path& launcherDirectory,
        const fs::path& relativePath,
        bool directory)
    {
        const fs::path alongside = AbsolutePath(launcherDirectory / relativePath);
        if (directory ? IsDirectory(alongside) : IsFile(alongside))
        {
            return alongside;
        }

        const fs::path development = AbsolutePath(
            launcherDirectory / L".." / L".." / relativePath);
        if (directory ? IsDirectory(development) : IsFile(development))
        {
            return development;
        }
        return alongside;
    }

    bool ParseOptions(int argc, wchar_t** argv, Options& options, std::wstring& error)
    {
        for (int index = 1; index < argc; ++index)
        {
            const std::wstring argument = argv[index];
            if (argument == L"--")
            {
                for (++index; index < argc; ++index)
                {
                    options.gameArguments.emplace_back(argv[index]);
                }
                break;
            }
            if (argument == L"--help" || argument == L"-h")
            {
                options.showHelp = true;
                continue;
            }
            if (argument == L"--dry-run")
            {
                options.dryRun = true;
                continue;
            }
            if (argument == L"--dual-instance-test")
            {
                options.dualInstanceTest = true;
                continue;
            }
            if (argument == L"--multiplayer-test")
            {
                options.multiplayerTest = true;
                continue;
            }
            if (argument == L"--multiplayer-roster-test")
            {
                options.multiplayerRosterTest = true;
                continue;
            }
            if (argument == L"--multiplayer-transition-test")
            {
                options.multiplayerTransitionTest = true;
                continue;
            }
            if (argument == L"--multiplayer-authority-test")
            {
                options.multiplayerAuthorityTest = true;
                continue;
            }
            if (argument == L"--multiplayer-combat-test")
            {
                options.multiplayerCombatTest = true;
                continue;
            }
            if (argument == L"--multiplayer-hero-will-test")
            {
                options.multiplayerHeroWillTest = true;
                continue;
            }
            if (argument == L"--multiplayer-playtest")
            {
                options.multiplayerPlaytest = true;
                continue;
            }
            if (argument == L"--host")
            {
                if (!options.multiplayerRole.empty())
                {
                    error = L"Choose either --host or --join, not both";
                    return false;
                }
                options.multiplayerRole = L"host";
                continue;
            }
            if (argument == L"--transform-probe")
            {
                if (!options.automationScenario.empty())
                {
                    error = L"--transform-probe cannot be combined with --automation";
                    return false;
                }
                options.transformationProbe = true;
                continue;
            }

            auto readPath = [&](fs::path& destination) -> bool
            {
                if (++index >= argc)
                {
                    error = L"Missing value after " + argument;
                    return false;
                }
                destination = argv[index];
                return true;
            };

            if (argument == L"--exe")
            {
                if (!readPath(options.executable)) return false;
            }
            else if (argument == L"--game-dir")
            {
                if (!readPath(options.gameDirectory)) return false;
            }
            else if (argument == L"--dll")
            {
                if (!readPath(options.clientDll)) return false;
            }
            else if (argument == L"--fixture-documents")
            {
                if (!readPath(options.fixtureDocuments)) return false;
            }
            else if (argument == L"--character-snapshot")
            {
                if (!readPath(options.characterSnapshot)) return false;
            }
            else if (argument == L"--local-instance")
            {
                if (++index >= argc)
                {
                    error = L"Missing identifier after --local-instance";
                    return false;
                }
                options.localInstance = argv[index];
            }
            else if (argument == L"--local-session")
            {
                if (++index >= argc)
                {
                    error = L"Missing identifier after --local-session";
                    return false;
                }
                options.localSession = argv[index];
            }
            else if (argument == L"--join")
            {
                if (!options.multiplayerRole.empty())
                {
                    error = L"Choose either --host or --join, not both";
                    return false;
                }
                if (++index >= argc)
                {
                    error = L"Missing IPv4 host address after --join";
                    return false;
                }
                options.multiplayerRole = L"guest";
                options.multiplayerAddress = argv[index];
            }
            else if (argument == L"--port")
            {
                if (++index >= argc)
                {
                    error = L"Missing UDP port after --port";
                    return false;
                }
                wchar_t* end = nullptr;
                const unsigned long value = std::wcstoul(argv[index], &end, 10);
                if (end == argv[index] || *end != L'\0' || value == 0 || value > 65'535)
                {
                    error = L"--port must be between 1 and 65535";
                    return false;
                }
                options.multiplayerPort = static_cast<unsigned short>(value);
            }
            else if (argument == L"--player-id")
            {
                if (++index >= argc || argv[index][0] == L'\0')
                {
                    error = L"Missing player identifier after --player-id";
                    return false;
                }
                options.multiplayerPlayerId = argv[index];
            }
            else if (argument == L"--appearance")
            {
                if (++index >= argc || argv[index][0] == L'\0')
                {
                    error = L"Missing creature definition after --appearance";
                    return false;
                }
                options.multiplayerAppearance = argv[index];
            }
            else if (argument == L"--hold")
            {
                if (++index >= argc)
                {
                    error = L"Missing seconds after --hold";
                    return false;
                }
                wchar_t* end = nullptr;
                const unsigned long value = std::wcstoul(argv[index], &end, 10);
                if (end == argv[index] || *end != L'\0' || value < 5 || value > 300)
                {
                    error = L"--hold must be between 5 and 300 seconds";
                    return false;
                }
                options.dualInstanceHoldSeconds = static_cast<unsigned int>(value);
            }
            else if (argument == L"--automation")
            {
                if (options.transformationProbe)
                {
                    error = L"--automation cannot be combined with --transform-probe";
                    return false;
                }
                if (++index >= argc)
                {
                    error = L"Missing scenario name after --automation";
                    return false;
                }
                options.automationScenario = argv[index];
                if (options.automationScenario.empty())
                {
                    error = L"Automation scenario cannot be empty";
                    return false;
                }
            }
            else if (argument == L"--timeout")
            {
                if (++index >= argc)
                {
                    error = L"Missing seconds after --timeout";
                    return false;
                }
                wchar_t* end = nullptr;
                const unsigned long value = std::wcstoul(argv[index], &end, 10);
                if (end == argv[index] || *end != L'\0' || value < 10 || value > 600)
                {
                    error = L"--timeout must be between 10 and 600 seconds";
                    return false;
                }
                options.automationTimeoutSeconds = static_cast<unsigned int>(value);
            }
            else
            {
                error = L"Unknown option: " + argument + L" (use -- before game arguments)";
                return false;
            }
        }
        if (!options.automationScenario.empty() &&
            options.automationScenario != L"observe_frontend" &&
            options.automationScenario != L"observe_save_list" &&
            options.automationScenario != L"bootstrap_fixture_probe" &&
            options.automationScenario != L"load_fixture" &&
            options.automationScenario != L"appearance_cycle")
        {
            error = L"Unknown automation scenario: " + options.automationScenario;
            return false;
        }
        if (!options.localInstance.empty())
        {
            const bool valid = options.localInstance.size() <= 32 &&
                std::all_of(
                    options.localInstance.begin(),
                    options.localInstance.end(),
                    [](wchar_t character)
                    {
                        return std::iswalnum(character) != 0 ||
                            character == L'-' || character == L'_';
                    });
            if (!valid)
            {
                error = L"--local-instance accepts at most 32 letters, digits, hyphens, or underscores";
                return false;
            }
        }
        if (!options.localSession.empty() && options.localInstance.empty())
        {
            error = L"--local-session requires --local-instance";
            return false;
        }
        if (!options.localSession.empty())
        {
            const bool valid = options.localSession.size() <= 64 &&
                std::all_of(
                    options.localSession.begin(),
                    options.localSession.end(),
                    [](wchar_t character)
                    {
                        return std::iswalnum(character) != 0 ||
                            character == L'-' || character == L'_' ||
                            character == L'.';
                    });
            if (!valid)
            {
                error = L"--local-session accepts at most 64 letters, digits, dots, hyphens, or underscores";
                return false;
            }
        }
        if (options.dualInstanceTest &&
            (!options.automationScenario.empty() ||
                options.transformationProbe ||
                !options.localInstance.empty()))
        {
            error = L"--dual-instance-test cannot be combined with automation, transform-probe, or --local-instance";
            return false;
        }
        if ((options.multiplayerTest || options.multiplayerRosterTest ||
                options.multiplayerTransitionTest ||
                options.multiplayerAuthorityTest ||
                options.multiplayerCombatTest ||
                options.multiplayerHeroWillTest ||
                options.multiplayerPlaytest) &&
            (options.dualInstanceTest || !options.multiplayerRole.empty() ||
                !options.automationScenario.empty() || options.transformationProbe ||
                !options.localInstance.empty()))
        {
            error = L"multiplayer test/playtest cannot be combined with another launch mode";
            return false;
        }
        const int multiplayerModes =
                (options.multiplayerTest ? 1 : 0) +
                (options.multiplayerRosterTest ? 1 : 0) +
                (options.multiplayerTransitionTest ? 1 : 0) +
                (options.multiplayerAuthorityTest ? 1 : 0) +
                (options.multiplayerCombatTest ? 1 : 0) +
                (options.multiplayerHeroWillTest ? 1 : 0) +
                (options.multiplayerPlaytest ? 1 : 0);
        const bool interactiveRosterPlaytest =
            options.multiplayerRosterTest && options.multiplayerPlaytest &&
            multiplayerModes == 2;
        const bool interactiveCombatPlaytest =
            options.multiplayerCombatTest && options.multiplayerPlaytest &&
            multiplayerModes == 2;
        if (multiplayerModes > 1 &&
            !interactiveRosterPlaytest && !interactiveCombatPlaytest)
        {
            error = L"Choose one multiplayer test or playtest mode";
            return false;
        }
        if (!options.multiplayerRole.empty() &&
            (!options.automationScenario.empty() || options.transformationProbe))
        {
            error = L"--host and --join cannot be combined with automation or transform-probe";
            return false;
        }
        if (options.multiplayerRole.empty() &&
            (!options.multiplayerPlayerId.empty() ||
                !options.multiplayerAppearance.empty()))
        {
            error = L"--player-id and --appearance require --host or --join";
            return false;
        }
        if (!options.multiplayerRole.empty())
        {
            if (options.multiplayerPlayerId.empty())
            {
                options.multiplayerPlayerId = options.multiplayerRole == L"host"
                    ? L"Host"
                    : L"Guest";
            }
            if (options.multiplayerAppearance.empty())
            {
                options.multiplayerAppearance = kRemoteHeroDefinition;
            }
        }
        if (!options.localInstance.empty() &&
            (!options.automationScenario.empty() || options.transformationProbe))
        {
            error = L"--local-instance cannot be combined with automation or transform-probe";
            return false;
        }
        const bool loadsFixture = options.automationScenario == L"load_fixture" ||
            options.automationScenario == L"appearance_cycle";
        if (!options.characterSnapshot.empty() &&
            !loadsFixture)
        {
            error = L"--character-snapshot is supported only with fixture-loading automation";
            return false;
        }
        return true;
    }

    fs::path ResolveExecutable(const Options& options, const fs::path& launcherDirectory)
    {
        if (!options.executable.empty())
        {
            return AbsolutePath(options.executable);
        }
        if (!options.gameDirectory.empty())
        {
            return ExecutableBelow(AbsolutePath(options.gameDirectory));
        }

        const fs::path alongside = ExecutableBelow(launcherDirectory);
        if (!alongside.empty())
        {
            return alongside;
        }
        return ExecutableBelow(fs::path(kDevelopmentGameRoot));
    }

    std::wstring QuoteArgument(const std::wstring& argument)
    {
        if (argument.find_first_of(L" \t\"") == std::wstring::npos)
        {
            return argument;
        }

        std::wstring quoted = L"\"";
        size_t backslashes = 0;
        for (const wchar_t character : argument)
        {
            if (character == L'\\')
            {
                ++backslashes;
            }
            else if (character == L'\"')
            {
                quoted.append(backslashes * 2 + 1, L'\\');
                quoted.push_back(L'\"');
                backslashes = 0;
            }
            else
            {
                quoted.append(backslashes, L'\\');
                backslashes = 0;
                quoted.push_back(character);
            }
        }
        quoted.append(backslashes * 2, L'\\');
        quoted.push_back(L'\"');
        return quoted;
    }

    std::wstring BuildCommandLine(const fs::path& executable, const std::vector<std::wstring>& arguments)
    {
        std::wstring commandLine = QuoteArgument(executable.wstring());
        for (const std::wstring& argument : arguments)
        {
            commandLine.push_back(L' ');
            commandLine.append(QuoteArgument(argument));
        }
        return commandLine;
    }

    void PrintUsage()
    {
        std::wcout
            << L"FableTogether.Launcher [options] [-- game arguments]\n\n"
            << L"  --game-dir <path>  Fable Anniversary root or Binaries\\Win32 directory\n"
            << L"  --exe <path>       Exact game executable path\n"
            << L"  --dll <path>       Exact client DLL path\n"
            << L"  --fixture-documents <dir>  Override the bundled adult-town save fixture\n"
            << L"  --character-snapshot <json>  Optional server-character state to apply after fixture load\n"
            << L"  --automation <id>  Run observe_frontend, observe_save_list, bootstrap_fixture_probe, load_fixture, or appearance_cycle\n"
            << L"  --timeout <sec>     Automation timeout from 10 to 600 seconds (default: 120)\n"
            << L"  --local-instance <id>  Start one isolated compact local development instance\n"
            << L"  --local-session <id>  Reuse a local development session identifier\n"
            << L"  --dual-instance-test  Prove isolated host and guest title windows coexist\n"
            << L"  --host              Host a multiplayer UDP session\n"
            << L"  --join <IPv4>       Join a host session\n"
            << L"  --port <port>       Multiplayer UDP port (default: 38171)\n"
            << L"  --player-id <name>  Multiplayer display identity\n"
            << L"  --appearance <id>   Stable creature definition used as this player's body\n"
            << L"  --multiplayer-test  Load two adult-town peers and prove remote locomotion\n"
            << L"  --multiplayer-roster-test  Load a host and two guests and prove guest-to-guest relay\n"
            << L"  --multiplayer-transition-test  Transition both peers and prove destination replication\n"
            << L"  --multiplayer-authority-test  Move only the host away and prove guest NPC ownership handoff\n"
            << L"  --multiplayer-combat-test  Attack from the guest and prove per-NPC authority handoff\n"
            << L"  --multiplayer-hero-will-test  Run only the Chamber Hero Will capture/replay sequence\n"
            << L"  --multiplayer-playtest  Set up and leave two connected peers running without synthetic input; combine with --multiplayer-combat-test for the Chamber arena\n"
            << L"  --hold <sec>        Dual-instance stability interval from 5 to 300 seconds (default: 10)\n"
            << L"  --transform-probe  Explicitly enable the unsafe number-row 1 experiment\n"
            << L"  --dry-run          Resolve and validate paths without launching\n"
            << L"  --help, -h         Show this help\n";
    }

    std::wstring CreateRunId()
    {
        SYSTEMTIME time = {};
        GetLocalTime(&time);
        wchar_t value[64] = {};
        swprintf_s(
            value,
            L"%04u%02u%02u-%02u%02u%02u-%03u-%lu",
            static_cast<unsigned int>(time.wYear),
            static_cast<unsigned int>(time.wMonth),
            static_cast<unsigned int>(time.wDay),
            static_cast<unsigned int>(time.wHour),
            static_cast<unsigned int>(time.wMinute),
            static_cast<unsigned int>(time.wSecond),
            static_cast<unsigned int>(time.wMilliseconds),
            static_cast<unsigned long>(GetCurrentProcessId()));
        return value;
    }

    bool InjectClient(
        HANDLE process,
        const fs::path& clientDll,
        HMODULE& remoteClientModule,
        std::wstring& error)
    {
        const std::wstring dllPath = clientDll.wstring();
        const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
        void* remotePath = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (remotePath == nullptr)
        {
            const DWORD code = GetLastError();
            error = L"VirtualAllocEx failed (" + std::to_wstring(code) + L"): " + FormatWindowsError(code);
            return false;
        }

        bool success = false;
        SIZE_T bytesWritten = 0;
        if (!WriteProcessMemory(process, remotePath, dllPath.c_str(), bytes, &bytesWritten) || bytesWritten != bytes)
        {
            const DWORD code = GetLastError();
            error = L"WriteProcessMemory failed (" + std::to_wstring(code) + L"): " + FormatWindowsError(code);
        }
        else
        {
            HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
            const auto loadLibrary = kernel32 == nullptr
                ? nullptr
                : reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"));
            if (loadLibrary == nullptr)
            {
                const DWORD code = GetLastError();
                error = L"Could not resolve LoadLibraryW (" + std::to_wstring(code) + L"): " + FormatWindowsError(code);
            }
            else
            {
                UniqueHandle remoteThread(CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath, 0, nullptr));
                if (!remoteThread.valid())
                {
                    const DWORD code = GetLastError();
                    error = L"CreateRemoteThread failed (" + std::to_wstring(code) + L"): " + FormatWindowsError(code);
                }
                else
                {
                    const DWORD waitResult = WaitForSingleObject(remoteThread.get(), kInjectionTimeoutMilliseconds);
                    if (waitResult != WAIT_OBJECT_0)
                    {
                        error = waitResult == WAIT_TIMEOUT
                            ? L"Timed out while loading the client DLL."
                            : L"Waiting for the injection thread failed.";
                    }
                    else
                    {
                        DWORD remoteResult = 0;
                        if (!GetExitCodeThread(remoteThread.get(), &remoteResult) || remoteResult == 0)
                        {
                            error = L"LoadLibraryW failed inside the game process.";
                        }
                        else
                        {
                            remoteClientModule = reinterpret_cast<HMODULE>(
                                static_cast<ULONG_PTR>(remoteResult));
                            success = true;
                        }
                    }
                }
            }
        }

        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return success;
    }

    bool InvokeInjectedClientExport(
        HANDLE process,
        HMODULE remoteClientModule,
        const fs::path& clientDll,
        const char* exportName,
        void* parameter,
        DWORD timeoutMilliseconds,
        DWORD expectedResult,
        DWORD& result,
        std::wstring& error)
    {
        if (process == nullptr || remoteClientModule == nullptr)
        {
            error = L"The injected client module handle was invalid.";
            return false;
        }

        // Resolve the exported entry point in a loader-only mapping. The
        // export RVA is stable between mappings, so adding it to the remote
        // LoadLibrary result lets the suspended process call its own code.
        HMODULE localImage = LoadLibraryExW(
            clientDll.c_str(),
            nullptr,
            DONT_RESOLVE_DLL_REFERENCES);
        if (localImage == nullptr)
        {
            const DWORD code = GetLastError();
            error = L"Could not inspect the client exports (" +
                std::to_wstring(code) + L"): " + FormatWindowsError(code);
            return false;
        }

        const FARPROC localEntry = GetProcAddress(localImage, exportName);
        const auto localBase = reinterpret_cast<std::uintptr_t>(localImage);
        const auto localAddress = reinterpret_cast<std::uintptr_t>(localEntry);
        const std::uintptr_t entryRva = localEntry != nullptr && localAddress >= localBase
            ? localAddress - localBase
            : 0;
        FreeLibrary(localImage);
        if (entryRva == 0)
        {
            error = L"The client DLL does not export the required startup entry point.";
            return false;
        }

        const auto remoteAddress = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            reinterpret_cast<std::uintptr_t>(remoteClientModule) + entryRva);
        UniqueHandle initializationThread(CreateRemoteThread(
            process,
            nullptr,
            0,
            remoteAddress,
            parameter,
            0,
            nullptr));
        if (!initializationThread.valid())
        {
            const DWORD code = GetLastError();
            error = L"CreateRemoteThread for client initialization failed (" +
                std::to_wstring(code) + L"): " + FormatWindowsError(code);
            return false;
        }

        const DWORD waitResult = WaitForSingleObject(
            initializationThread.get(),
            timeoutMilliseconds);
        if (waitResult != WAIT_OBJECT_0)
        {
            error = waitResult == WAIT_TIMEOUT
                ? L"Timed out while initializing the client DLL."
                : L"Waiting for client initialization failed.";
            return false;
        }

        if (!GetExitCodeThread(initializationThread.get(), &result))
        {
            const DWORD code = GetLastError();
            error = L"Could not read the client startup result (" +
                std::to_wstring(code) + L"): " + FormatWindowsError(code);
            return false;
        }
        if (result != expectedResult)
        {
            wchar_t detail[96] = {};
            swprintf_s(
                detail,
                L"The client startup entry point returned 0x%08lX; expected 0x%08lX.",
                static_cast<unsigned long>(result),
                static_cast<unsigned long>(expectedResult));
            error = detail;
            return false;
        }
        return true;
    }

    bool InitializeInjectedClient(
        HANDLE process,
        HMODULE remoteClientModule,
        const fs::path& clientDll,
        std::wstring& error)
    {
        DWORD result = 0;
        return InvokeInjectedClientExport(
            process,
            remoteClientModule,
            clientDll,
            "FableTogetherInitialize",
            nullptr,
            kInjectionTimeoutMilliseconds,
            kClientPreResumeReady,
            result,
            error);
    }

    bool WaitForInjectedClientReady(
        HANDLE process,
        HMODULE remoteClientModule,
        const fs::path& clientDll,
        std::wstring& error)
    {
        DWORD result = 0;
        return InvokeInjectedClientExport(
            process,
            remoteClientModule,
            clientDll,
            "FableTogetherWaitForReady",
            reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(kRuntimeReadyTimeoutMilliseconds)),
            kRuntimeReadyTimeoutMilliseconds + 5'000,
            kClientRuntimeReady,
            result,
            error);
    }

    struct ProcessWindowSearch
    {
        DWORD processId = 0;
        HWND bestWindow = nullptr;
        unsigned long long bestArea = 0;
        bool bestVisible = false;
    };

    BOOL CALLBACK FindProcessWindow(HWND window, LPARAM parameter)
    {
        auto& search = *reinterpret_cast<ProcessWindowSearch*>(parameter);
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId != search.processId)
        {
            return TRUE;
        }

        wchar_t className[64] = {};
        GetClassNameW(window, className, static_cast<int>(std::size(className)));
        if (std::wcscmp(className, L"#32770") == 0)
        {
            return TRUE;
        }

        RECT client = {};
        if (!GetClientRect(window, &client))
        {
            return TRUE;
        }
        const LONG width = client.right - client.left;
        const LONG height = client.bottom - client.top;
        const auto area = width > 0 && height > 0
            ? static_cast<unsigned long long>(width) * static_cast<unsigned long long>(height)
            : 0;
        const bool visible = IsWindowVisible(window) != FALSE;
        if ((visible && !search.bestVisible) ||
            (visible == search.bestVisible && area > search.bestArea))
        {
            search.bestArea = area;
            search.bestWindow = window;
            search.bestVisible = visible;
        }
        return TRUE;
    }

    HWND FindMainWindow(DWORD processId)
    {
        ProcessWindowSearch search;
        search.processId = processId;
        EnumWindows(FindProcessWindow, reinterpret_cast<LPARAM>(&search));
        return search.bestWindow;
    }

    std::string ReadEventFile(const fs::path& eventPath)
    {
        std::ifstream stream(eventPath, std::ios::binary);
        if (!stream)
        {
            return {};
        }
        return std::string(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
    }

    bool EventWasReported(const std::string& content, const char* state)
    {
        const std::string marker = std::string("\"state\":\"") + state + "\"";
        return content.find(marker) != std::string::npos;
    }

    std::size_t EventCount(const std::string& content, const char* state)
    {
        const std::string marker = std::string("\"state\":\"") + state + "\"";
        std::size_t count = 0;
        std::size_t position = 0;
        while ((position = content.find(marker, position)) !=
            std::string::npos)
        {
            ++count;
            position += marker.size();
        }
        return count;
    }

    bool EventDetailContains(
        const std::string& content,
        const char* state,
        const char* detail)
    {
        const std::string stateMarker =
            std::string("\"state\":\"") + state + "\"";
        std::size_t position = 0;
        while ((position = content.find(stateMarker, position)) !=
            std::string::npos)
        {
            const std::size_t end = content.find('\n', position);
            const std::size_t length = end == std::string::npos
                ? std::string::npos
                : end - position;
            if (content.substr(position, length).find(detail) !=
                std::string::npos)
            {
                return true;
            }
            position += stateMarker.size();
        }
        return false;
    }

    std::size_t EventDetailCount(
        const std::string& content,
        const char* state,
        const char* detail)
    {
        const std::string stateMarker =
            std::string("\"state\":\"") + state + "\"";
        std::size_t count = 0;
        std::size_t position = 0;
        while ((position = content.find(stateMarker, position)) !=
            std::string::npos)
        {
            const std::size_t end = content.find('\n', position);
            const std::size_t length = end == std::string::npos
                ? std::string::npos
                : end - position;
            if (content.substr(position, length).find(detail) !=
                std::string::npos)
            {
                ++count;
            }
            position += stateMarker.size();
        }
        return count;
    }

    bool LastEventDetailContains(
        const std::string& content,
        const char* state,
        const char* detail)
    {
        const std::string stateMarker =
            std::string("\"state\":\"") + state + "\"";
        const std::size_t position = content.rfind(stateMarker);
        if (position == std::string::npos)
        {
            return false;
        }
        const std::size_t end = content.find('\n', position);
        const std::size_t length = end == std::string::npos
            ? std::string::npos
            : end - position;
        return content.substr(position, length).find(detail) !=
            std::string::npos;
    }

    std::uint64_t StablePlayerActorId(
        const std::wstring& role,
        const std::wstring& playerId)
    {
        const int required = WideCharToMultiByte(
            CP_UTF8, 0, playerId.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return 0;
        }
        std::string utf8(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, playerId.c_str(), -1, utf8.data(), required, nullptr,
            nullptr);
        utf8.pop_back();
        std::uint64_t hash = 14695981039346656037ull;
        for (const unsigned char character : utf8)
        {
            hash ^= character;
            hash *= 1099511628211ull;
        }
        hash ^= role == L"host" ? 1u : 2u;
        hash *= 1099511628211ull;
        return hash == 0 ? 1 : hash;
    }

    std::string PvpReactionDetail(
        std::uint64_t sourceActorId,
        std::uint64_t targetActorId)
    {
        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "source_kind=1 source=%016llX target_kind=1 target=%016llX reaction_route=observer-replay",
            static_cast<unsigned long long>(sourceActorId),
            static_cast<unsigned long long>(targetActorId));
        return detail;
    }

    bool ReplicatedMovementWasApplied(
        const std::string& content,
        std::uint64_t actorId = 0)
    {
        const std::string actorMarker = actorId == 0
            ? std::string()
            : "actor_id=" + std::to_string(actorId);
        const std::array<const char*, 2> markers = {
            "\"state\":\"CreatureMovementFacingRouted\"",
            "\"state\":\"CreatureBackgroundReplicatedMovementDriven\""};
        for (const char* const marker : markers)
        {
            std::size_t position = 0;
            while ((position = content.find(marker, position)) !=
                std::string::npos)
            {
                const std::size_t end = content.find('\n', position);
                const std::string event = content.substr(
                    position,
                    end == std::string::npos
                        ? std::string::npos
                        : end - position);
                const std::size_t motion = event.find("linear_velocity=(");
                if ((actorMarker.empty() ||
                        event.find(actorMarker) != std::string::npos) &&
                    motion != std::string::npos &&
                    event.find(
                        "linear_velocity=(0.000000,0.000000,0.000000)",
                        motion) == std::string::npos)
                {
                    return true;
                }
                position += std::strlen(marker);
            }
        }
        return false;
    }

    bool CloseCreatedProcess(HANDLE process, DWORD processId, HANDLE shutdownEvent)
    {
        if (shutdownEvent != nullptr)
        {
            std::wcout << L"Automation: requesting shutdown through the run-scoped client event.\n";
            if (!SetEvent(shutdownEvent))
            {
                std::wcerr << L"Automation: could not signal the run-scoped shutdown event.\n";
            }
        }
        else
        {
            HWND window = nullptr;
            const ULONGLONG windowDeadline = GetTickCount64() + 2'000;
            do
            {
                window = FindMainWindow(processId);
                if (window != nullptr)
                {
                    break;
                }
                if (WaitForSingleObject(process, 100) == WAIT_OBJECT_0)
                {
                    return true;
                }
            } while (GetTickCount64() < windowDeadline);

            if (window != nullptr)
            {
                std::wcout << L"Automation: requesting graceful shutdown through the game window.\n";
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
        }

        if (WaitForSingleObject(process, 15'000) == WAIT_OBJECT_0)
        {
            return true;
        }

        std::wcerr << L"Automation: graceful shutdown timed out; terminating only PID "
                   << processId << L".\n";
        TerminateProcess(process, ERROR_TIMEOUT);
        WaitForSingleObject(process, 5'000);
        return false;
    }

    int RunAutomation(
        HANDLE process,
        DWORD processId,
        const fs::path& eventPath,
        const std::wstring& scenario,
        unsigned int timeoutSeconds,
        HANDLE shutdownEvent,
        bool characterSnapshotExpected)
    {
        std::wcout << L"Automation: waiting up to " << timeoutSeconds
                   << L" seconds for scenario " << scenario << L".\n";
        const ULONGLONG deadline =
            GetTickCount64() + static_cast<ULONGLONG>(timeoutSeconds) * 1'000;
        ScopedSyntheticKey movementKey('W');
        ScopedSyntheticMouseButton attackButton;
        bool movementInputSubmitted = false;
        ULONGLONG movementInputPressedAt = 0;
        unsigned int attackInputAttempts = 0;
        ULONGLONG attackInputPressedAt = 0;
        ULONGLONG attackInputReleasedAt = 0;

        for (;;)
        {
            const std::string events = ReadEventFile(eventPath);
            if (scenario == L"appearance_cycle")
            {
                if (!movementInputSubmitted &&
                    EventWasReported(events, "AppearanceFormReady"))
                {
                    const HWND window = FindMainWindow(processId);
                    if (window != nullptr)
                    {
                        SetForegroundWindow(window);
                    }
                    if (!movementKey.Press())
                    {
                        std::wcerr << L"Automation failed: could not press W for the native NPC movement probe.\n";
                        CloseCreatedProcess(process, processId, shutdownEvent);
                        return 1;
                    }
                    movementInputSubmitted = true;
                    movementInputPressedAt = GetTickCount64();
                    std::wcout << L"Automation: holding W briefly to test native NPC locomotion ownership.\n";
                }
                if (movementKey.down() &&
                    GetTickCount64() - movementInputPressedAt >= 750)
                {
                    if (!movementKey.Release())
                    {
                        std::wcerr << L"Automation failed: could not release W after the native NPC movement probe.\n";
                        CloseCreatedProcess(process, processId, shutdownEvent);
                        return 1;
                    }
                    std::wcout << L"Automation: released W after the native NPC movement probe.\n";
                }
                if (movementInputSubmitted && !movementKey.down() &&
                    !attackButton.down() &&
                    !EventWasReported(events, "PlayerAttackAbilityIntercepted") &&
                    attackInputAttempts < 5 &&
                    (attackInputAttempts == 0 ||
                        GetTickCount64() - attackInputReleasedAt >= 250) &&
                    EventWasReported(events, "CreaturePlayerCombatRouterBound"))
                {
                    const HWND window = FindMainWindow(processId);
                    if (window != nullptr)
                    {
                        SetForegroundWindow(window);
                    }
                    if (!attackButton.Press(window))
                    {
                        std::wcerr << L"Automation failed: could not submit the mapped ATTACK stimulus.\n";
                        CloseCreatedProcess(process, processId, shutdownEvent);
                        return 1;
                    }
                    ++attackInputAttempts;
                    attackInputPressedAt = GetTickCount64();
                    std::wcout << L"Automation: submitted mapped game-window ATTACK stimulus "
                               << attackInputAttempts
                               << L"/5 for the native combat boundary.\n";
                }
                if (attackButton.down() &&
                    GetTickCount64() - attackInputPressedAt >= 100)
                {
                    if (!attackButton.Release())
                    {
                        std::wcerr << L"Automation failed: could not release the mapped ATTACK stimulus.\n";
                        CloseCreatedProcess(process, processId, shutdownEvent);
                        return 1;
                    }
                    attackInputReleasedAt = GetTickCount64();
                }
            }
            if (EventWasReported(events, "ClientFailed"))
            {
                std::wcerr << L"Automation failed: the injected client reported a hook failure.\n";
                CloseCreatedProcess(process, processId, shutdownEvent);
                return 1;
            }

            if (scenario == L"observe_frontend" && EventWasReported(events, "FrontendReady"))
            {
                std::wcout << L"Automation: Fable front-end main menu reached.\n";
                const bool graceful = CloseCreatedProcess(process, processId, shutdownEvent);
                const std::string finalEvents = ReadEventFile(eventPath);
                if (!graceful || !EventWasReported(finalEvents, "ShutdownStarted"))
                {
                    std::wcerr << L"Automation failed: front end was reached, but shutdown was not cleanly observed.\n";
                    return 1;
                }
                std::wcout << L"Automation passed: front end reached and process shut down cleanly.\n";
                return 0;
            }

            if (scenario == L"observe_save_list" && EventWasReported(events, "SaveListReady"))
            {
                std::wcout << L"Automation: Fable Load Game save list reached without selecting a save.\n";
                const bool graceful = CloseCreatedProcess(process, processId, shutdownEvent);
                const std::string finalEvents = ReadEventFile(eventPath);
                if (!graceful || !EventWasReported(finalEvents, "ShutdownStarted"))
                {
                    std::wcerr << L"Automation failed: save list was reached, but shutdown was not observed.\n";
                    return 1;
                }
                std::wcout << L"Automation passed: save list observed and process shut down.\n";
                return 0;
            }

            if (scenario == L"bootstrap_fixture_probe" &&
                EventWasReported(events, "HeroReady"))
            {
                std::wcout << L"Automation: isolated New Game reached a resolvable Hero in the playable world.\n";
                const bool graceful = CloseCreatedProcess(process, processId, shutdownEvent);
                const std::string finalEvents = ReadEventFile(eventPath);
                if (!graceful || !EventWasReported(finalEvents, "ShutdownStarted"))
                {
                    std::wcerr << L"Automation failed: Hero became ready, but shutdown was not observed.\n";
                    return 1;
                }
                std::wcout << L"Automation passed: isolated New Game Hero readiness observed.\n";
                return 0;
            }

            const bool loadFixturePassed = characterSnapshotExpected
                ? EventWasReported(events, "CharacterSnapshotAssertionPassed")
                : EventWasReported(events, "AssertionPassed");
            if (scenario == L"load_fixture" && loadFixturePassed)
            {
                std::wcout << (characterSnapshotExpected
                    ? L"Automation: server-character snapshot produced stable target transform and combat health.\n"
                    : L"Automation: exact isolated AutoSave produced stable Hero transform and active-creature state.\n");
                const bool graceful = CloseCreatedProcess(process, processId, shutdownEvent);
                const std::string finalEvents = ReadEventFile(eventPath);
                if (!graceful || !EventWasReported(finalEvents, "ShutdownStarted"))
                {
                    std::wcerr << L"Automation failed: loaded fixture assertions passed, but shutdown was not observed.\n";
                    return 1;
                }
                std::wcout << (characterSnapshotExpected
                    ? L"Automation passed: exact isolated AutoSave loaded, the server-character snapshot was applied and verified, and the process shut down.\n"
                    : L"Automation passed: exact isolated AutoSave loaded, Hero state was verified, and the process shut down.\n");
                return 0;
            }

            if (scenario == L"appearance_cycle" &&
                EventWasReported(events, "AppearanceCyclePassed"))
            {
                std::wcout << L"Automation: AngelScript created guard, villager, and hobbe forms; verified Hero frame displacement produced native guard navigator requests, physical displacement, locomotion input, and animation-state activity while the authoritative Hero remained stable.\n";
                const bool graceful = CloseCreatedProcess(process, processId, shutdownEvent);
                const std::string finalEvents = ReadEventFile(eventPath);
                if (!graceful || !EventWasReported(finalEvents, "ShutdownStarted") ||
                    !EventWasReported(finalEvents, "PlayerAttackAbilityHookReady") ||
                    !EventWasReported(finalEvents, "PlayerAttackAbilityIntercepted"))
                {
                    std::wcerr << L"Automation failed: appearance assertions passed, but deep native player ATTACK ability interception or clean shutdown was not observed.\n";
                    return 1;
                }
                std::wcout << L"Automation passed: native locomotion, player-owned facing, hidden-Hero shadow follow, friendly decision ownership, native player ATTACK-to-NPC ability routing, three-form cycling, Hero restoration, and clean shutdown were all observed.\n";
                return 0;
            }

            const DWORD processState = WaitForSingleObject(process, 250);
            if (processState == WAIT_OBJECT_0)
            {
                DWORD exitCode = 0;
                GetExitCodeProcess(process, &exitCode);
                std::wcerr << L"Automation failed: Fable exited before the scenario completed; exit code "
                           << exitCode << L".\n";
                return 1;
            }
            if (processState == WAIT_FAILED)
            {
                std::wcerr << L"Automation failed while monitoring the Fable process.\n";
                CloseCreatedProcess(process, processId, shutdownEvent);
                return 1;
            }
            if (GetTickCount64() >= deadline)
            {
                std::wcerr << L"Automation failed: scenario timed out.\n";
                CloseCreatedProcess(process, processId, shutdownEvent);
                return 1;
            }
        }
    }

    struct LaunchedGame final
    {
        UniqueHandle process;
        UniqueHandle shutdownEvent;
        DWORD processId = 0;
        HWND window = nullptr;
    };

    bool SpawnGame(
        const fs::path& executable,
        const fs::path& clientDll,
        const fs::path& clientLog,
        const fs::path& eventPath,
        const fs::path& fixtureDocuments,
        const fs::path& characterSnapshot,
        const fs::path& scriptData,
        const std::wstring& clientMode,
        const std::wstring& scenario,
        const std::wstring& runId,
        const std::wstring& localSession,
        const std::wstring& localInstance,
        const std::wstring& multiplayerRole,
        const std::wstring& multiplayerAddress,
        unsigned short multiplayerPort,
        const std::wstring& multiplayerPlayerId,
        const std::wstring& multiplayerAppearance,
        const std::vector<std::wstring>& arguments,
        LaunchedGame& launched)
    {
        launched = {};
        const fs::path gameDefinitions = multiplayerRole.empty()
            ? fs::path()
            : AbsolutePath(clientDll.parent_path() / L"definitions" / L"game.bin");
        if (!multiplayerRole.empty() && !IsFile(gameDefinitions))
        {
            std::wcerr
                << L"Multiplayer requires the remote-Hero definitions sidecar: "
                << gameDefinitions.wstring() << L'\n';
            return false;
        }
        std::error_code logError;
        fs::remove(clientLog, logError);
        if (logError)
        {
            std::wcerr << L"Log:    could not clear the previous log: "
                       << logError.message().c_str() << L'\n';
        }

        std::wstring commandLine = BuildCommandLine(executable, arguments);
        std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back(L'\0');

        STARTUPINFOW startupInfo = {};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo = {};
        const std::wstring workingDirectory = executable.parent_path().wstring();
        ScopedEnvironmentVariable steamAppId(L"SteamAppId", kFableSteamAppId);
        ScopedEnvironmentVariable steamGameId(L"SteamGameId", kFableSteamAppId);
        ScopedEnvironmentVariable modeEnvironment(kClientModeEnvironment, clientMode.c_str());
        ScopedEnvironmentVariable scenarioEnvironment(kScenarioEnvironment, scenario.c_str());
        ScopedEnvironmentVariable runIdEnvironment(kRunIdEnvironment, runId.c_str());
        ScopedEnvironmentVariable eventPathEnvironment(kEventPathEnvironment, eventPath.c_str());
        ScopedEnvironmentVariable logPathEnvironment(kLogPathEnvironment, clientLog.c_str());
        ScopedEnvironmentVariable fixtureDocumentsEnvironment(
            kFixtureDocumentsEnvironment,
            fixtureDocuments.c_str());
        ScopedEnvironmentVariable characterSnapshotEnvironment(
            kCharacterSnapshotEnvironment,
            characterSnapshot.c_str());
        ScopedEnvironmentVariable scriptDataEnvironment(
            kScriptDataEnvironment,
            scriptData.c_str());
        ScopedEnvironmentVariable localSessionEnvironment(
            kLocalSessionEnvironment,
            localSession.c_str());
        ScopedEnvironmentVariable localInstanceEnvironment(
            kLocalInstanceEnvironment,
            localInstance.c_str());
        const std::wstring multiplayerPortText = multiplayerRole.empty()
            ? std::wstring()
            : std::to_wstring(multiplayerPort);
        ScopedEnvironmentVariable multiplayerRoleEnvironment(
            kMultiplayerRoleEnvironment,
            multiplayerRole.c_str());
        ScopedEnvironmentVariable multiplayerAddressEnvironment(
            kMultiplayerAddressEnvironment,
            multiplayerAddress.c_str());
        ScopedEnvironmentVariable multiplayerPortEnvironment(
            kMultiplayerPortEnvironment,
            multiplayerPortText.c_str());
        ScopedEnvironmentVariable multiplayerPlayerIdEnvironment(
            kMultiplayerPlayerIdEnvironment,
            multiplayerPlayerId.c_str());
        ScopedEnvironmentVariable multiplayerAppearanceEnvironment(
            kMultiplayerAppearanceEnvironment,
            multiplayerAppearance.c_str());
        ScopedEnvironmentVariable gameDefinitionsEnvironment(
            kGameDefinitionsEnvironment,
            gameDefinitions.c_str());
        if (!steamAppId.applied() || !steamGameId.applied() ||
            !modeEnvironment.applied() || !scenarioEnvironment.applied() ||
            !runIdEnvironment.applied() || !eventPathEnvironment.applied() ||
            !logPathEnvironment.applied() ||
            !fixtureDocumentsEnvironment.applied() ||
            !characterSnapshotEnvironment.applied() ||
            !scriptDataEnvironment.applied() ||
            !localSessionEnvironment.applied() ||
            !localInstanceEnvironment.applied() ||
            !multiplayerRoleEnvironment.applied() ||
            !multiplayerAddressEnvironment.applied() ||
            !multiplayerPortEnvironment.applied() ||
            !multiplayerPlayerIdEnvironment.applied() ||
            !multiplayerAppearanceEnvironment.applied() ||
            !gameDefinitionsEnvironment.applied())
        {
            std::wcerr << L"Could not prepare the child-process environment for Fable Anniversary.\n";
            return false;
        }

        UniqueHandle shutdownEvent;
        if (!scenario.empty())
        {
            const std::wstring shutdownEventName = kShutdownEventPrefix + runId;
            shutdownEvent = UniqueHandle(CreateEventW(
                nullptr,
                TRUE,
                FALSE,
                shutdownEventName.c_str()));
            if (shutdownEvent.get() == nullptr)
            {
                std::wcerr << L"Could not create the run-scoped automation shutdown event.\n";
                return false;
            }
        }

        std::wcout << L"Steam:  App ID " << kFableSteamAppId << L" supplied to the child process.\n";
        if (!localInstance.empty())
        {
            std::wcout << L"Identity: local development peer " << localInstance
                       << L"; Steam identity is not used for peer identity.\n";
        }
        std::wcout << L"Launch: creating Fable Anniversary suspended; working directory "
                   << workingDirectory << L".\n";
        if (!CreateProcessW(
                executable.c_str(),
                mutableCommandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_SUSPENDED,
                nullptr,
                workingDirectory.c_str(),
                &startupInfo,
                &processInfo))
        {
            const DWORD code = GetLastError();
            std::wcerr << L"Failed to start Fable Anniversary (" << code << L"): " << FormatWindowsError(code) << L'\n';
            return false;
        }

        UniqueHandle process(processInfo.hProcess);
        UniqueHandle primaryThread(processInfo.hThread);
        std::wcout << L"Launch: suspended process created (PID " << processInfo.dwProcessId << L").\n";
        std::wcout << L"Inject: loading " << clientDll.wstring() << L".\n";
        std::wcout.flush();
        std::wstring injectionError;
        HMODULE remoteClientModule = nullptr;
        if (!InjectClient(process.get(), clientDll, remoteClientModule, injectionError))
        {
            TerminateProcess(process.get(), ERROR_DLL_INIT_FAILED);
            WaitForSingleObject(process.get(), 5'000);
            std::wcerr << L"Injection failed; the suspended game process was terminated: " << injectionError << L'\n';
            return false;
        }
        std::wcout << L"Inject: client DLL loaded; initializing before resume.\n";
        std::wcout.flush();
        if (!InitializeInjectedClient(
                process.get(),
                remoteClientModule,
                clientDll,
                injectionError))
        {
            TerminateProcess(process.get(), ERROR_DLL_INIT_FAILED);
            WaitForSingleObject(process.get(), 5'000);
            std::wcerr << L"Client initialization failed; the suspended game process was terminated: "
                       << injectionError << L'\n';
            return false;
        }
        std::wcout << L"Inject: pre-resume initialization validated; resuming the primary thread.\n";

        if (ResumeThread(primaryThread.get()) == static_cast<DWORD>(-1))
        {
            const DWORD code = GetLastError();
            TerminateProcess(process.get(), code);
            std::wcerr << L"Could not resume the game (" << code << L"): " << FormatWindowsError(code) << L'\n';
            return false;
        }

        std::wcout << L"Inject: waiting for the client runtime to become ready.\n";
        std::wcout.flush();
        if (!WaitForInjectedClientReady(
                process.get(),
                remoteClientModule,
                clientDll,
                injectionError))
        {
            TerminateProcess(process.get(), ERROR_DLL_INIT_FAILED);
            WaitForSingleObject(process.get(), 5'000);
            std::wcerr << L"Client runtime startup failed; the game process was terminated: "
                       << injectionError << L'\n';
            return false;
        }

        std::wcout << L"Fable Anniversary started with FableTogether.Client.dll; runtime ready (PID "
                   << processInfo.dwProcessId << L").\n";
        launched.process = std::move(process);
        launched.shutdownEvent = std::move(shutdownEvent);
        launched.processId = processInfo.dwProcessId;
        return true;
    }

    bool PositionLocalWindow(
        HWND window,
        const wchar_t* instance,
        int x,
        int y)
    {
        if (window == nullptr)
        {
            return false;
        }

        std::wstring title = L"Fable Anniversary - FableTogether local ";
        title.append(instance != nullptr ? instance : L"instance");
        SetWindowTextW(window, title.c_str());

        // The launcher itself is DPI-unaware, so SetWindowPos would otherwise
        // virtualize 830x620 to 1245x930 at the development desktop's 150%
        // scale. Temporarily use a DPI-aware caller context: these constants
        // then mean actual screen pixels and two clients really fit side by
        // side.
        const DPI_AWARENESS_CONTEXT previousDpiContext =
            SetThreadDpiAwarenessContext(
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const bool positioned = SetWindowPos(
            window,
            HWND_TOP,
            x,
            y,
            kLocalTestWindowWidth,
            kLocalTestWindowHeight,
            SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_NOACTIVATE) != FALSE;
        if (previousDpiContext != nullptr)
        {
            SetThreadDpiAwarenessContext(previousDpiContext);
        }
        return positioned;
    }

    bool WindowIsResponsive(HWND window)
    {
        if (window == nullptr || !IsWindow(window) || IsHungAppWindow(window))
        {
            return false;
        }
        DWORD_PTR ignored = 0;
        return SendMessageTimeoutW(
            window,
            WM_NULL,
            0,
            0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            1'000,
            &ignored) != 0;
    }

    bool WaitForLocalInstanceReady(
        LaunchedGame& game,
        const fs::path& eventPath,
        const wchar_t* instance,
        int x,
        unsigned int timeoutSeconds)
    {
        const ULONGLONG deadline = GetTickCount64() +
            static_cast<ULONGLONG>(timeoutSeconds) * 1'000;
        for (;;)
        {
            const std::string events = ReadEventFile(eventPath);
            if (EventWasReported(events, "ClientFailed"))
            {
                std::wcerr << L"Local instance " << instance
                           << L" reported a client hook failure.\n";
                return false;
            }
            if (EventWasReported(events, "ClientHooksReady") &&
                EventWasReported(events, "FrontEndStartReady") &&
                EventWasReported(events, "LocalInstanceReady") &&
                EventWasReported(events, "UnrealSingletonNamespaced") &&
                EventWasReported(events, "FixtureDocumentsRedirectReady") &&
                EventWasReported(events, "ScriptStorageRootReady"))
            {
                game.window = FindMainWindow(game.processId);
                if (game.window != nullptr &&
                    PositionLocalWindow(game.window, instance, x, 0) &&
                    WindowIsResponsive(game.window))
                {
                    return true;
                }
            }

            if (WaitForSingleObject(game.process.get(), 250) == WAIT_OBJECT_0)
            {
                DWORD exitCode = 0;
                GetExitCodeProcess(game.process.get(), &exitCode);
                std::wcerr << L"Local instance " << instance
                           << L" exited before its title window was ready; exit code "
                           << exitCode << L".\n";
                return false;
            }
            if (GetTickCount64() >= deadline)
            {
                std::wcerr << L"Local instance " << instance
                           << L" timed out before its title window was ready.\n";
                return false;
            }
        }
    }

    bool RepositionLocalInstanceWindow(
        LaunchedGame& game,
        const wchar_t* instance,
        int x,
        int y = 0,
        unsigned int timeoutMilliseconds = 5'000)
    {
        const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;
        do
        {
            const HWND currentWindow = FindMainWindow(game.processId);
            if (currentWindow != nullptr)
            {
                game.window = currentWindow;
            }
            if (game.window != nullptr && IsWindow(game.window) &&
                PositionLocalWindow(game.window, instance, x, y))
            {
                return true;
            }
            if (!game.process.valid() ||
                WaitForSingleObject(game.process.get(), 100) == WAIT_OBJECT_0)
            {
                return false;
            }
        } while (GetTickCount64() < deadline);
        return false;
    }

    bool AnyFableProcessIsRunning()
    {
        UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!snapshot.valid())
        {
            return true;
        }
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        if (!Process32FirstW(snapshot.get(), &entry))
        {
            return true;
        }
        do
        {
            if (_wcsicmp(entry.szExeFile, kGameExecutableName) == 0)
            {
                return true;
            }
        } while (Process32NextW(snapshot.get(), &entry));
        return false;
    }

    std::vector<std::wstring> LocalWindowArguments(
        const std::vector<std::wstring>& original)
    {
        std::vector<std::wstring> arguments = original;
        const auto addIfMissing = [&](const wchar_t* value)
        {
            const bool present = std::any_of(
                arguments.begin(),
                arguments.end(),
                [value](const std::wstring& argument)
                {
                    return _wcsicmp(argument.c_str(), value) == 0;
                });
            if (!present)
            {
                arguments.emplace_back(value);
            }
        };
        addIfMissing(L"-windowed");
        addIfMissing(L"-ResX=1280");
        addIfMissing(L"-ResY=720");
        addIfMissing(L"-nomoviestartup");
        return arguments;
    }

    bool WaitForMultiplayerEvent(
        LaunchedGame& game,
        const fs::path& eventPath,
        const wchar_t* instance,
        const char* expectedState,
        unsigned int timeoutSeconds)
    {
        const ULONGLONG deadline = GetTickCount64() +
            static_cast<ULONGLONG>(timeoutSeconds) * 1'000;
        for (;;)
        {
            const std::string events = ReadEventFile(eventPath);
            if (EventWasReported(events, "ClientFailed"))
            {
                std::wcerr << L"Multiplayer " << instance
                           << L" reported a client failure while waiting for "
                           << expectedState << L".\n";
                return false;
            }
            if (EventWasReported(events, expectedState))
            {
                return true;
            }
            const DWORD state = WaitForSingleObject(game.process.get(), 250);
            if (state == WAIT_OBJECT_0)
            {
                DWORD exitCode = 0;
                GetExitCodeProcess(game.process.get(), &exitCode);
                std::wcerr << L"Multiplayer " << instance
                           << L" exited while waiting for " << expectedState
                           << L"; exit code " << exitCode << L".\n";
                return false;
            }
            if (state == WAIT_FAILED || GetTickCount64() >= deadline)
            {
                std::wcerr << L"Multiplayer " << instance
                           << L" timed out while waiting for " << expectedState
                           << L".\n";
                return false;
            }
        }
    }

    bool WaitForMultiplayerEventCount(
        LaunchedGame& game,
        const fs::path& eventPath,
        const wchar_t* instance,
        const char* expectedState,
        std::size_t expectedCount,
        unsigned int timeoutSeconds)
    {
        const ULONGLONG deadline = GetTickCount64() +
            static_cast<ULONGLONG>(timeoutSeconds) * 1'000;
        for (;;)
        {
            const std::string events = ReadEventFile(eventPath);
            if (EventWasReported(events, "ClientFailed"))
            {
                std::wcerr << L"Multiplayer " << instance
                           << L" reported a client failure while waiting for "
                           << expectedState << L" count " << expectedCount
                           << L".\n";
                return false;
            }
            if (EventCount(events, expectedState) >= expectedCount)
            {
                return true;
            }
            const DWORD state = WaitForSingleObject(game.process.get(), 250);
            if (state == WAIT_OBJECT_0)
            {
                DWORD exitCode = 0;
                GetExitCodeProcess(game.process.get(), &exitCode);
                std::wcerr << L"Multiplayer " << instance
                           << L" exited while waiting for " << expectedState
                           << L" count " << expectedCount << L"; exit code "
                           << exitCode << L".\n";
                return false;
            }
            if (state == WAIT_FAILED || GetTickCount64() >= deadline)
            {
                std::wcerr << L"Multiplayer " << instance
                           << L" timed out while waiting for " << expectedState
                           << L" count " << expectedCount << L".\n";
                return false;
            }
        }
    }

    bool WaitForMultiplayerEventDetail(
        LaunchedGame& game,
        const fs::path& eventPath,
        const wchar_t* instance,
        const char* expectedState,
        const std::string& expectedDetail,
        unsigned int timeoutSeconds)
    {
        const ULONGLONG deadline = GetTickCount64() +
            static_cast<ULONGLONG>(timeoutSeconds) * 1'000;
        for (;;)
        {
            const std::string events = ReadEventFile(eventPath);
            if (EventWasReported(events, "ClientFailed"))
            {
                std::wcerr << L"Multiplayer " << instance
                           << L" reported a client failure while waiting for "
                           << expectedState << L".\n";
                return false;
            }
            if (EventDetailContains(
                    events, expectedState, expectedDetail.c_str()))
            {
                return true;
            }
            const DWORD state = WaitForSingleObject(game.process.get(), 250);
            if (state == WAIT_OBJECT_0)
            {
                DWORD exitCode = 0;
                GetExitCodeProcess(game.process.get(), &exitCode);
                std::wcerr << L"Multiplayer " << instance
                           << L" exited while waiting for " << expectedState
                           << L"; exit code " << exitCode << L".\n";
                return false;
            }
            if (state == WAIT_FAILED || GetTickCount64() >= deadline)
            {
                std::wcerr << L"Multiplayer " << instance
                           << L" timed out while waiting for " << expectedState
                           << L" with the required actor detail.\n";
                return false;
            }
        }
    }

    bool WaitForMultiplayerEventDetailCount(
        LaunchedGame& game,
        const fs::path& eventPath,
        const wchar_t* instance,
        const char* expectedState,
        const std::string& expectedDetail,
        std::size_t expectedCount,
        unsigned int timeoutSeconds)
    {
        const ULONGLONG deadline = GetTickCount64() +
            static_cast<ULONGLONG>(timeoutSeconds) * 1'000;
        for (;;)
        {
            const std::string events = ReadEventFile(eventPath);
            if (EventWasReported(events, "ClientFailed"))
            {
                std::wcerr << L"Multiplayer " << instance
                           << L" reported a client failure while waiting for "
                           << expectedState << L" count " << expectedCount
                           << L".\n";
                return false;
            }
            if (EventDetailCount(
                    events,
                    expectedState,
                    expectedDetail.c_str()) >= expectedCount)
            {
                return true;
            }
            const DWORD state = WaitForSingleObject(game.process.get(), 250);
            if (state == WAIT_OBJECT_0)
            {
                DWORD exitCode = 0;
                GetExitCodeProcess(game.process.get(), &exitCode);
                std::wcerr << L"Multiplayer " << instance
                           << L" exited while waiting for " << expectedState
                           << L" count " << expectedCount << L"; exit code "
                           << exitCode << L".\n";
                return false;
            }
            if (state == WAIT_FAILED || GetTickCount64() >= deadline)
            {
                std::wcerr << L"Multiplayer " << instance
                           << L" timed out while waiting for " << expectedState
                           << L" count " << expectedCount
                           << L" with the required detail.\n";
                return false;
            }
        }
    }

    bool WaitForBackgroundMovement(
        LaunchedGame& game,
        const fs::path& eventPath,
        const wchar_t* instance,
        std::uint64_t actorId,
        unsigned int timeoutSeconds)
    {
        const ULONGLONG deadline = GetTickCount64() +
            static_cast<ULONGLONG>(timeoutSeconds) * 1'000;
        for (;;)
        {
            const std::string events = ReadEventFile(eventPath);
            if (EventWasReported(events, "ClientFailed"))
            {
                std::wcerr << L"Multiplayer " << instance
                           << L" reported a client failure while waiting for "
                              L"unfocused remote movement.\n";
                return false;
            }
            if (ReplicatedMovementWasApplied(events, actorId))
            {
                return true;
            }
            const DWORD state = WaitForSingleObject(game.process.get(), 250);
            if (state == WAIT_OBJECT_0)
            {
                DWORD exitCode = 0;
                GetExitCodeProcess(game.process.get(), &exitCode);
                std::wcerr << L"Multiplayer " << instance
                           << L" exited while waiting for unfocused remote "
                              L"movement; exit code " << exitCode << L".\n";
                return false;
            }
            if (state == WAIT_FAILED || GetTickCount64() >= deadline)
            {
                std::wcerr << L"Multiplayer " << instance
                           << L" timed out while waiting for unfocused remote "
                              L"movement.\n";
                return false;
            }
        }
    }

    bool MoveMultiplayerPeer(
        LaunchedGame& game,
        const wchar_t* instance,
        unsigned int durationMilliseconds = 1'250,
        bool moveLaterally = true)
    {
        const HWND currentWindow = FindMainWindow(game.processId);
        if (currentWindow != nullptr)
        {
            game.window = currentWindow;
        }
        if (game.window == nullptr || !IsWindow(game.window))
        {
            std::wcerr << L"Multiplayer " << instance
                       << L" has no game window for movement.\n";
            return false;
        }
        const auto activate = [](HWND window)
        {
            if (GetForegroundWindow() == window)
            {
                return true;
            }
            MSG message = {};
            PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
            const DWORD currentThread = GetCurrentThreadId();
            const DWORD targetThread = GetWindowThreadProcessId(window, nullptr);
            const HWND previousForeground = GetForegroundWindow();
            const DWORD foregroundThread = previousForeground != nullptr
                ? GetWindowThreadProcessId(previousForeground, nullptr)
                : 0;
            const bool attachedForeground = foregroundThread != 0 &&
                foregroundThread != currentThread &&
                AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;
            const bool attachedTarget = targetThread != 0 &&
                targetThread != currentThread &&
                targetThread != foregroundThread &&
                AttachThreadInput(currentThread, targetThread, TRUE) != FALSE;
            ShowWindowAsync(window, SW_RESTORE);
            BringWindowToTop(window);
            SetForegroundWindow(window);
            SetFocus(window);
            if (attachedTarget)
            {
                AttachThreadInput(currentThread, targetThread, FALSE);
            }
            if (attachedForeground)
            {
                AttachThreadInput(currentThread, foregroundThread, FALSE);
            }
            if (GetForegroundWindow() != window)
            {
                INPUT alt[2] = {};
                alt[0].type = INPUT_KEYBOARD;
                alt[0].ki.wVk = VK_MENU;
                alt[1] = alt[0];
                alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(2, alt, sizeof(INPUT));
                SetForegroundWindow(window);
            }
            const ULONGLONG deadline = GetTickCount64() + 1'000;
            while (GetForegroundWindow() != window && GetTickCount64() < deadline)
            {
                Sleep(25);
            }
            return GetForegroundWindow() == window;
        };
        if (!activate(game.window))
        {
            std::wcerr << L"Could not activate multiplayer " << instance << L".\n";
            return false;
        }
        Sleep(250);
        // Both local acceptance peers load the same fixture save and therefore
        // begin at the exact same transform between a wall and the Bowerstone
        // Jail transition. Take a short forward-right diagonal before handoff:
        // collision slides the Hero away from the wall without retreating into
        // the map boundary.
        ScopedSyntheticKey forward('W');
        ScopedSyntheticKey lateral('D');
        if (!forward.Press() || (moveLaterally && !lateral.Press()))
        {
            forward.Release();
            lateral.Release();
            std::wcerr << L"Could not press movement input in multiplayer " << instance << L".\n";
            return false;
        }
        std::wcout << L"Multiplayer test: moving " << instance
                   << L" through Fable's normal player input.\n";
        Sleep(durationMilliseconds);
        const bool forwardReleased = forward.Release();
        const bool lateralReleased = !moveLaterally || lateral.Release();
        if (!forwardReleased || !lateralReleased)
        {
            std::wcerr << L"Could not release movement input in multiplayer " << instance << L".\n";
            return false;
        }
        return true;
    }

    bool FocusMultiplayerPeer(
        LaunchedGame& game,
        const wchar_t* instance)
    {
        const HWND currentWindow = FindMainWindow(game.processId);
        if (currentWindow != nullptr)
        {
            game.window = currentWindow;
        }
        if (game.window == nullptr || !IsWindow(game.window))
        {
            std::wcerr << L"Multiplayer " << instance
                       << L" has no game window to activate.\n";
            return false;
        }
        MSG message = {};
        PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
        const DWORD currentThread = GetCurrentThreadId();
        const DWORD targetThread = GetWindowThreadProcessId(game.window, nullptr);
        const HWND previousForeground = GetForegroundWindow();
        const DWORD foregroundThread = previousForeground != nullptr
            ? GetWindowThreadProcessId(previousForeground, nullptr)
            : 0;
        const bool attachedForeground = foregroundThread != 0 &&
            foregroundThread != currentThread &&
            AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;
        const bool attachedTarget = targetThread != 0 &&
            targetThread != currentThread &&
            targetThread != foregroundThread &&
            AttachThreadInput(currentThread, targetThread, TRUE) != FALSE;
        ShowWindowAsync(game.window, SW_RESTORE);
        BringWindowToTop(game.window);
        SetForegroundWindow(game.window);
        SetFocus(game.window);
        if (attachedTarget)
        {
            AttachThreadInput(currentThread, targetThread, FALSE);
        }
        if (attachedForeground)
        {
            AttachThreadInput(currentThread, foregroundThread, FALSE);
        }
        if (GetForegroundWindow() != game.window)
        {
            INPUT alt[2] = {};
            alt[0].type = INPUT_KEYBOARD;
            alt[0].ki.wVk = VK_MENU;
            alt[1] = alt[0];
            alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, alt, sizeof(INPUT));
            SetForegroundWindow(game.window);
        }
        const ULONGLONG deadline = GetTickCount64() + 1'000;
        while (GetForegroundWindow() != game.window && GetTickCount64() < deadline)
        {
            Sleep(25);
        }
        if (GetForegroundWindow() != game.window)
        {
            std::wcerr << L"Windows refused to activate multiplayer "
                       << instance << L".\n";
            return false;
        }
        Sleep(500);
        return true;
    }

    bool DriveFriendlyTargetedPvpAttacks(
        LaunchedGame& game,
        const wchar_t* instance,
        const fs::path& events,
        unsigned int attacks,
        unsigned int timeoutSeconds)
    {
        if (attacks == 0)
        {
            return false;
        }

        for (unsigned int ordinal = 1; ordinal <= attacks; ++ordinal)
        {
            const std::string attackOrdinal =
                "ordinal=" + std::to_string(ordinal) + " action=SPACE";
            if (!WaitForMultiplayerEventDetail(
                    game,
                    events,
                    instance,
                    "MultiplayerCombatPvpTargetInputRequested",
                    attackOrdinal.c_str(),
                    timeoutSeconds) ||
                !FocusMultiplayerPeer(game, instance))
            {
                return false;
            }

            // Fable's Space action owns friendly target acquisition. Keep it
            // held until the injected acceptance driver has observed the
            // remote Hero in the native targeting component and accepted the
            // corresponding attack; this prevents an untargeted swing from
            // racing target selection.
            ScopedSyntheticKey targetAction(VK_SPACE);
            if (!targetAction.Press())
            {
                std::wcerr << L"Could not press the friendly-target action in multiplayer "
                           << instance << L".\n";
                return false;
            }
            const bool targetObserved = WaitForMultiplayerEventDetail(
                game,
                events,
                instance,
                "MultiplayerCombatPvpFriendlyTargetObserved",
                attackOrdinal.c_str(),
                timeoutSeconds);
            const bool targetReleased = targetAction.Release();
            if (!targetObserved || !targetReleased)
            {
                if (!targetReleased)
                {
                    std::wcerr << L"Could not release the friendly-target action in multiplayer "
                               << instance << L".\n";
                }
                return false;
            }
            Sleep(250);
        }
        return true;
    }

    int RunMultiplayerTest(
        const fs::path& executable,
        const fs::path& clientDll,
        const fs::path& fixtureDocumentsSource,
        const fs::path& sessionRoot,
        const std::wstring& sessionId,
        unsigned short port,
        unsigned int timeoutSeconds,
        bool interactive,
        bool rosterTest,
        bool transitionTest,
        bool authorityTest,
        bool combatTest,
        bool heroWillTest,
        const std::vector<std::wstring>& originalArguments)
    {
        const bool sixPeerShowcase = interactive && rosterTest;
        ScopedEnvironmentVariable manualPlaytestEnvironment(
            kManualPlaytestEnvironment,
            interactive ? L"1" : L"");
        if (!manualPlaytestEnvironment.applied())
        {
            std::wcerr << L"Could not configure manual multiplayer input ownership.\n";
            return 1;
        }
        const std::vector<std::wstring> arguments =
            LocalWindowArguments(originalArguments);
        const auto roleRoot = [&](const wchar_t* role)
        {
            return sessionRoot / role;
        };
        const auto prepareRole = [&](const wchar_t* role) -> bool
        {
            const fs::path root = roleRoot(role);
            std::error_code error;
            fs::create_directories(root / L"Documents", error);
            if (!error)
            {
                fs::create_directories(root / L"script-data", error);
            }
            if (!error)
            {
                fs::copy(
                    fixtureDocumentsSource,
                    root / L"Documents",
                    fs::copy_options::recursive |
                        fs::copy_options::overwrite_existing,
                    error);
            }
            return !error;
        };
        if (!prepareRole(L"host") || !prepareRole(L"guest") ||
            (rosterTest && (!prepareRole(L"guest2") ||
                (sixPeerShowcase && (!prepareRole(L"guest3") ||
                    !prepareRole(L"guest4") || !prepareRole(L"guest5"))))))
        {
            std::wcerr << L"Could not prepare multiplayer fixture documents.\n";
            return 1;
        }

        LaunchedGame host;
        LaunchedGame guest;
        LaunchedGame guest2;
        std::array<LaunchedGame, 3> showcaseGuests;
        const auto stop = [](LaunchedGame& game)
        {
            if (game.process.valid())
            {
                CloseCreatedProcess(
                    game.process.get(),
                    game.processId,
                    game.shutdownEvent.get());
            }
        };
        const auto spawnRole = [&](
            const wchar_t* instance,
            const wchar_t* scenario,
            const wchar_t* role,
            const wchar_t* address,
            const wchar_t* player,
            const wchar_t* appearance,
            LaunchedGame& game) -> bool
        {
            const fs::path root = roleRoot(instance);
            const std::wstring runId = sessionId + L"-" + instance;
            return SpawnGame(
                executable,
                clientDll,
                root / L"client.log",
                root / L"events.jsonl",
                root / L"Documents",
                {},
                root / L"script-data",
                L"observe",
                scenario,
                runId,
                sessionId,
                instance,
                role,
                address,
                port,
                player,
                appearance,
                arguments,
                game);
        };
        const auto stopAll = [&]
        {
            for (auto& showcaseGuest : showcaseGuests)
            {
                stop(showcaseGuest);
            }
            stop(guest2);
            stop(guest);
            stop(host);
        };

        std::wcout << L"Multiplayer test: starting host.\n";
        if (!spawnRole(
                L"host",
                heroWillTest
                    ? L"multiplayer_host_hero_will"
                : combatTest
                    ? L"multiplayer_host_combat"
                : authorityTest
                    ? L"multiplayer_host_authority"
                : transitionTest
                    ? L"multiplayer_host_transition"
                    : L"multiplayer_host",
                L"host",
                L"",
                L"Host",
                kRemoteHeroDefinition,
                host) ||
            !WaitForLocalInstanceReady(
                host,
                roleRoot(L"host") / L"events.jsonl",
                L"host",
                0,
                timeoutSeconds))
        {
            stop(host);
            return 1;
        }

        // A real host owns an already-loaded world before a guest joins it.
        // Starting the second DX9 window while Continue is replacing the
        // host's front-end world can steal focus inside a retail engine state
        // predicate whose gameplay manager is transiently null.  Let the host
        // finish the selected-save transition before launching the guest; this
        // also makes the local acceptance lifecycle match the product flow.
        const fs::path hostEvents = roleRoot(L"host") / L"events.jsonl";
        if (!WaitForMultiplayerEvent(
                host,
                hostEvents,
                L"host",
                "MultiplayerLocalHeroReady",
                timeoutSeconds))
        {
            stop(host);
            return 1;
        }
        std::wcout
            << L"Multiplayer test: host selected-save Hero is in-world; starting guest.\n";
        if (!spawnRole(
                L"guest",
                heroWillTest
                    ? L"multiplayer_guest_hero_will"
                : combatTest
                    ? L"multiplayer_guest_combat"
                : authorityTest
                    ? L"multiplayer_guest_authority"
                : transitionTest
                    ? L"multiplayer_guest_transition"
                    : L"multiplayer_guest",
                L"guest",
                L"127.0.0.1",
                L"Guest",
                kRemoteHeroDefinition,
                guest) ||
            !WaitForLocalInstanceReady(
                guest,
                roleRoot(L"guest") / L"events.jsonl",
                L"guest",
                kLocalTestWindowPitch,
                timeoutSeconds))
        {
            stop(guest);
            stop(host);
            return 1;
        }

        const fs::path guestEvents = roleRoot(L"guest") / L"events.jsonl";
        const bool localHeroesReady = WaitForMultiplayerEvent(
                guest, guestEvents, L"guest", "MultiplayerLocalHeroReady", timeoutSeconds);
        bool worldsReady = localHeroesReady &&
            (interactive || transitionTest || authorityTest || combatTest || heroWillTest ||
                FocusMultiplayerPeer(host, L"host")) &&
            WaitForMultiplayerEvent(
                host, hostEvents, L"host", "MultiplayerRemoteDefinitionCreated", timeoutSeconds) &&
            (interactive || transitionTest || authorityTest || combatTest || heroWillTest ||
                FocusMultiplayerPeer(guest, L"guest")) &&
            WaitForMultiplayerEvent(
                guest, guestEvents, L"guest", "MultiplayerRemoteDefinitionCreated", timeoutSeconds);
        if (worldsReady && rosterTest)
        {
            // A guest can present the host before the host republishes the
            // current saved-map baseline. Fence roster expansion on that
            // baseline so the next guest can resolve the host's native map
            // identity during its own save construction barrier.
            worldsReady =
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerSavedEntityMapBaselinePublished",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerSavedEntityMapBaselineAccepted",
                    timeoutSeconds);
        }
        const fs::path guest2Events = roleRoot(L"guest2") / L"events.jsonl";
        if (worldsReady && rosterTest)
        {
            std::wcout
                << L"Multiplayer roster test: starting a second independently owned guest.\n";
            worldsReady = spawnRole(
                    L"guest2",
                    L"multiplayer_guest",
                    L"guest",
                    L"127.0.0.1",
                    L"Guest Two",
                    kRemoteHeroDefinition,
                    guest2) &&
                WaitForLocalInstanceReady(
                    guest2,
                    guest2Events,
                    L"guest2",
                    kLocalTestWindowPitch * 2,
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest2,
                    guest2Events,
                    L"guest2",
                    "MultiplayerLocalHeroReady",
                    timeoutSeconds);
            if (worldsReady)
            {
                const std::uint64_t guestActorId =
                    StablePlayerActorId(L"guest", L"Guest");
                const std::uint64_t guest2ActorId =
                    StablePlayerActorId(L"guest", L"Guest Two");
                worldsReady =
                    WaitForMultiplayerEventDetail(
                        host,
                        hostEvents,
                        L"host",
                        "MultiplayerRemoteDefinitionCreated",
                        "actor_id=" + std::to_string(guest2ActorId),
                        timeoutSeconds) &&
                    WaitForMultiplayerEventDetail(
                        guest,
                        guestEvents,
                        L"guest",
                        "MultiplayerRemoteDefinitionCreated",
                        "actor_id=" + std::to_string(guest2ActorId),
                        timeoutSeconds) &&
                    WaitForMultiplayerEventDetail(
                        guest2,
                        guest2Events,
                        L"guest2",
                        "MultiplayerRemoteDefinitionCreated",
                        "actor_id=" + std::to_string(guestActorId),
                        timeoutSeconds);
            }

            if (worldsReady && sixPeerShowcase)
            {
                struct ShowcasePeer
                {
                    const wchar_t* instance;
                    const wchar_t* player;
                    LaunchedGame* game;
                };
                const std::array<ShowcasePeer, 3> extraPeers = {{
                    {L"guest3", L"Guest Three", &showcaseGuests[0]},
                    {L"guest4", L"Guest Four", &showcaseGuests[1]},
                    {L"guest5", L"Guest Five", &showcaseGuests[2]},
                }};
                for (const auto& peer : extraPeers)
                {
                    const std::wstring scenario = L"multiplayer_guest";
                    worldsReady = spawnRole(
                            peer.instance,
                            scenario.c_str(),
                            L"guest",
                            L"127.0.0.1",
                            peer.player,
                            kRemoteHeroDefinition,
                            *peer.game) &&
                        WaitForLocalInstanceReady(
                            *peer.game,
                            roleRoot(peer.instance) / L"events.jsonl",
                            peer.instance,
                            kLocalTestWindowPitch *
                                (static_cast<int>(peer.instance[5] - L'2')),
                            timeoutSeconds) &&
                        WaitForMultiplayerEvent(
                            *peer.game,
                            roleRoot(peer.instance) / L"events.jsonl",
                            peer.instance,
                            "MultiplayerLocalHeroReady",
                            timeoutSeconds);
                    if (!worldsReady)
                    {
                        break;
                    }
                }
                if (worldsReady)
                {
                    struct PeerView
                    {
                        const wchar_t* instance;
                        const wchar_t* player;
                        LaunchedGame* game;
                    };
                    const std::array<PeerView, 6> peers = {{
                        {L"host", L"Host", &host},
                        {L"guest", L"Guest", &guest},
                        {L"guest2", L"Guest Two", &guest2},
                        {L"guest3", L"Guest Three", &showcaseGuests[0]},
                        {L"guest4", L"Guest Four", &showcaseGuests[1]},
                        {L"guest5", L"Guest Five", &showcaseGuests[2]},
                    }};
                    for (std::size_t observer = 0;
                         observer < peers.size() && worldsReady;
                         ++observer)
                    {
                        const fs::path events =
                            roleRoot(peers[observer].instance) / L"events.jsonl";
                        for (std::size_t subject = 0;
                             subject < peers.size(); ++subject)
                        {
                            if (observer == subject)
                            {
                                continue;
                            }
                            worldsReady = WaitForMultiplayerEventDetail(
                                *peers[observer].game,
                                events,
                                peers[observer].instance,
                                "MultiplayerRemoteDefinitionCreated",
                                "actor_id=" + std::to_string(
                                    StablePlayerActorId(
                                        subject == 0 ? L"host" : L"guest",
                                        peers[subject].player)),
                                timeoutSeconds);
                            if (!worldsReady)
                            {
                                break;
                            }
                        }
                    }
                }
            }
        }
        if (!worldsReady)
        {
            stopAll();
            return 1;
        }

        // UE3 reapplies its configured client size while the selected save is
        // replacing the front-end world. Reassert the compact development
        // layout only after every gameplay window exists so both perspectives
        // remain visible side-by-side for the entire test.
        bool windowsPositioned =
            RepositionLocalInstanceWindow(host, L"host", 0) &&
            RepositionLocalInstanceWindow(
                guest, L"guest", kLocalTestWindowPitch);
        if (rosterTest)
        {
            windowsPositioned = windowsPositioned &&
                RepositionLocalInstanceWindow(
                    guest2,
                    L"guest2",
                    kLocalTestWindowPitch * 2);
            if (sixPeerShowcase)
            {
                windowsPositioned =
                    RepositionLocalInstanceWindow(host, L"host", 0, 0) &&
                    RepositionLocalInstanceWindow(
                        guest, L"guest", kLocalTestWindowWidth, 0) &&
                    RepositionLocalInstanceWindow(
                        guest2, L"guest2", kLocalTestWindowWidth * 2, 0);
                for (std::size_t index = 0; index < showcaseGuests.size(); ++index)
                {
                    windowsPositioned = windowsPositioned &&
                        RepositionLocalInstanceWindow(
                            showcaseGuests[index],
                            (index == 0 ? L"guest3" :
                                index == 1 ? L"guest4" : L"guest5"),
                            kLocalTestWindowWidth *
                                static_cast<int>(index),
                            kLocalTestWindowHeight);
                }
            }
        }
        if (!windowsPositioned)
        {
            std::wcerr
                << L"Could not reapply the compact side-by-side layout after world load; continuing with the layout established at startup.\n";
        }

        // The combat fixture loads both Heroes from the same Chamber save.
        // Wait until both selected-save Heroes and their remote presentations
        // are live, then separate only the host through normal player input.
        if (combatTest && !interactive &&
            !MoveMultiplayerPeer(host, L"host", 500, false))
        {
            stopAll();
            return 1;
        }

        std::wcout << (rosterTest
            ? L"Multiplayer roster test: every process created a native presentation for each other player actor.\n"
            : L"Multiplayer test: both remote definitions were created from the safe game dispatch context at distinct native transforms.\n");
        if (combatTest || heroWillTest)
        {
            // The isolated fixture already starts both peers in the Chamber of
            // Fate; stage them around that saved floor position before combat.
            const bool combatPeersStaged =
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerCombatPeerStaged",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatPeerStaged",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerCombatArenaConverged",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatArenaConverged",
                    timeoutSeconds);
            const std::string combatTargetScript =
                "script_name=SCRIPT_NAME_FABLE_TOGETHER_COMBAT_TARGET";
            const bool targetReady = combatPeersStaged &&
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerCombatTargetSpawned",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerEntityMaterialized",
                    combatTargetScript,
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatTargetArmed",
                    timeoutSeconds);
            if (interactive)
            {
                if (!targetReady)
                {
                    stopAll();
                    return 1;
                }
                std::wcout
                    << L"Manual Chamber combat playtest is ready. Host PID "
                    << host.processId << L", guest PID " << guest.processId
                    << L". Both processes are being left running.\n"
                    << L"The Hobbe is armed, but no synthetic movement, targeting, equipment, health, or combat input will be submitted.\n"
                    << L"State root: " << sessionRoot.wstring() << L"\n";
                return 0;
            }
            const std::uint64_t guestActorId =
                StablePlayerActorId(L"guest", L"Guest");
            const std::string guestActor = std::to_string(guestActorId);
            const std::string guestCombatOwner =
                "kind=5 authority_actor_id=" +
                guestActor + " map=FrescoDome";
            const std::string guestVitalsOwner = "owner=" + guestActor;
            const std::string guestPlayerVitals =
                "subject=player actor=" + guestActor;
            const std::string guestRemoteVitals = "actor=" + guestActor;
            const std::string guestRemoteCompanion =
                "actor_id=" + guestActor;
            const std::size_t guestPlayerVitalsBeforeAttack =
                EventDetailCount(
                    ReadEventFile(guestEvents),
                    "MultiplayerEntityVitalsPublished",
                    guestPlayerVitals.c_str());
            const std::size_t hostRemoteVitalsBeforeAttack =
                EventDetailCount(
                    ReadEventFile(hostEvents),
                    "MultiplayerRemotePlayerVitalsApplied",
                    guestRemoteVitals.c_str());
            const bool pvpTargetingCompleted = targetReady &&
                (heroWillTest ||
                    (DriveFriendlyTargetedPvpAttacks(
                        host,
                        L"host",
                        hostEvents,
                        2,
                        timeoutSeconds) &&
                     DriveFriendlyTargetedPvpAttacks(
                        guest,
                        L"guest",
                        guestEvents,
                        2,
                        timeoutSeconds)));
            const bool attackSubmitted = heroWillTest
                ? pvpTargetingCompleted
                : pvpTargetingCompleted &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatNativeUntargetedAttackSubmitted",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerLocalPlayerActionCaptured",
                    "native_action=CCreatureAction_Interruptable",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemoteNativeUntargetedAttackSubmitted",
                    "source_action=CCreatureAction_Interruptable",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatNativeAttackSubmitted",
                    timeoutSeconds);
            const bool handoffCompleted = heroWillTest
                ? targetReady
                : attackSubmitted &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatNativeMeleeReady",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerLocalPlayerActionCaptured",
                    "native_action=CCreatureAction_Interruptable",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemotePlayerAbilitySubmitted",
                    "ability_id=1101",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemoteNativeAttackSubmitted",
                    "route=retail-ai-immediate-attack submitted=true",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatNativeSustainedAttackSubmitted",
                    "ordinal=6/6",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemoteNativeUntargetedAttackSubmitted",
                    "submitted=true",
                    7,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemotePlayerAbilitySubmitted",
                    "ability_id=1101",
                    8,
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatNativeMeleeStowed",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatNativeMeleeRedrawReady",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerLocalWeaponTransitionCaptured",
                    "native_action=CCreatureAction_",
                    3,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemoteWeaponTransitionSubmitted",
                    "animation_id=",
                    3,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemoteWeaponTransitionAnimationStarted",
                    "native_action=CCreatureAction_",
                    3,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemoteWeaponTransitionApplied",
                    "attempts=",
                    3,
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatEngagementRequested",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerActionAuthorityChanged",
                    guestCombatOwner,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerActionAuthorityChanged",
                    guestCombatOwner,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerEntitySimulationCoverage",
                    "fenced=1",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerEntitySimulationCoverage",
                    "local_simulation=1",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerEntityActionBegan",
                    guestVitalsOwner,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemoteCompanionRegistered",
                    guestRemoteCompanion,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerEntityVitalsPublished",
                    guestVitalsOwner,
                    2,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerEntityVitalsAccepted",
                    guestVitalsOwner,
                    2,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerEntityVitalsApplied",
                    guestVitalsOwner,
                    1,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatTargetHealthMutationApplied",
                    "source=native-creature-health-setter",
                    2,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerEntityVitalsPublished",
                    guestPlayerVitals,
                    guestPlayerVitalsBeforeAttack + 1,
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatGuestHealthMutationApplied",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemotePlayerVitalsApplied",
                    guestRemoteVitals,
                    hostRemoteVitalsBeforeAttack + 1,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerEntityActionEnded",
                    "PlayerAttackEngagement",
                    timeoutSeconds);
            const std::uint64_t hostActorId =
                StablePlayerActorId(L"host", L"Host");
            const std::string hostActor = std::to_string(hostActorId);
            const bool visualExchangeCompleted = handoffCompleted &&
                (heroWillTest
                    ? WaitForMultiplayerEvent(
                        host,
                        hostEvents,
                        L"host",
                        "MultiplayerHeroWillSequenceArmed",
                        timeoutSeconds) &&
                      WaitForMultiplayerEvent(
                        guest,
                        guestEvents,
                        L"guest",
                        "MultiplayerHeroWillSequenceArmed",
                        timeoutSeconds)
                    :
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerCombatHeroAttackSubmitted",
                    "source=host-local-hero target=enemy ordinal=2",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatHeroAttackSubmitted",
                    "source=guest-local-hero target=enemy ordinal=2",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerCombatEnemyCounterattackSubmitted",
                    "target=host-local-hero",
                    2,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerCombatEnemyCounterattackSubmitted",
                    "target=guest-remote-hero",
                    2,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerEntityNativeActionSubmitted",
                    ("target_player=" + hostActor).c_str(),
                    2,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerEntityNativeActionSubmitted",
                    ("target_player=" + guestActor).c_str(),
                    2,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerLocalPlayerActionCaptured",
                    ("target_player=" + guestActor).c_str(),
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerLocalPlayerActionCaptured",
                    ("target_player=" + hostActor).c_str(),
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerRemotePlayerAbilitySubmitted",
                    ("actor_id=" + hostActor).c_str(),
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemotePlayerAbilitySubmitted",
                    ("actor_id=" + guestActor).c_str(),
                    timeoutSeconds) &&
                WaitForMultiplayerEventCount(
                    host,
                    hostEvents,
                    L"host",
                    "CreatureHitResolved",
                    1,
                    timeoutSeconds) &&
                WaitForMultiplayerEventCount(
                    guest,
                    guestEvents,
                    L"guest",
                    "CreatureHitResolved",
                    1,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerCombatHitApplied",
                    "target_kind=1",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatHitApplied",
                    "target_kind=1",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerCombatHitApplied",
                    "target_kind=2",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatHitApplied",
                    "target_kind=2",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerCombatHitApplied",
                    PvpReactionDetail(hostActorId, guestActorId),
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatHitApplied",
                    PvpReactionDetail(guestActorId, hostActorId),
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerCombatVisualExchangeComplete",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatVisualExchangeComplete",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerCombatTargetKillStarted",
                    "script_name=SCRIPT_NAME_FABLE_TOGETHER_COMBAT_TARGET",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerCombatTargetTerminalObserved",
                    "script_name=SCRIPT_NAME_FABLE_TOGETHER_COMBAT_TARGET health=0.000 maximum=60.000 dead=true",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerCombatTargetTerminalObserved",
                    "script_name=SCRIPT_NAME_FABLE_TOGETHER_COMBAT_TARGET health=0.000 maximum=60.000 dead=true",
                    timeoutSeconds));
            wchar_t pillarOnlyValue[8] = {};
            const DWORD pillarOnlyLength = GetEnvironmentVariableW(
                kHeroWillPillarOnlyEnvironment,
                pillarOnlyValue,
                static_cast<DWORD>(std::size(pillarOnlyValue)));
            const bool pillarOnly = heroWillTest && pillarOnlyLength != 0 &&
                pillarOnlyLength < std::size(pillarOnlyValue) &&
                pillarOnlyValue[0] == L'1';
            const char* const heroWillCompletion = pillarOnly
                ? "accepted=2 expected_unsupported=0 total=2"
                : "accepted=17 expected_unsupported=2 total=19";
            const std::size_t expectedHeroWillUses = pillarOnly ? 2 : 17;
            const bool heroWillSequenceCompleted = visualExchangeCompleted &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerHeroWillSequenceComplete",
                    heroWillCompletion,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerHeroWillSequenceComplete",
                    heroWillCompletion,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerLocalHeroAbilityCaptured",
                    "command=1",
                    expectedHeroWillUses,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerLocalHeroAbilityCaptured",
                    "command=1",
                    expectedHeroWillUses,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemoteHeroAbilityReplayed",
                    "command=1",
                    expectedHeroWillUses,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerRemoteHeroAbilityReplayed",
                    "command=1",
                    expectedHeroWillUses,
                    timeoutSeconds);
            if (heroWillSequenceCompleted)
            {
                Sleep(2'000);
            }
            const std::string hostEventContent = ReadEventFile(hostEvents);
            const std::string guestEventContent = ReadEventFile(guestEvents);
            const bool authorityReturned = heroWillSequenceCompleted &&
                (heroWillTest ||
                    (LastEventDetailContains(
                        hostEventContent,
                        "MultiplayerEntitySimulationCoverage",
                        "fenced=0") &&
                     LastEventDetailContains(
                        guestEventContent,
                        "MultiplayerEntitySimulationCoverage",
                        "local_simulation=0")));
            host.window = FindMainWindow(host.processId);
            guest.window = FindMainWindow(guest.processId);
            const bool peersSurvived = authorityReturned &&
                WaitForSingleObject(host.process.get(), 0) == WAIT_TIMEOUT &&
                WaitForSingleObject(guest.process.get(), 0) == WAIT_TIMEOUT &&
                host.window != nullptr && guest.window != nullptr &&
                WindowIsResponsive(host.window) &&
                WindowIsResponsive(guest.window) &&
                !EventWasReported(hostEventContent, "ClientFailed") &&
                !EventWasReported(guestEventContent, "ClientFailed");
            if (interactive)
            {
                std::wcout
                    << (peersSurvived
                        ? L"Interactive combat sequence completed; leaving both peers running for visual verification.\n"
                        : L"Interactive combat sequence did not satisfy every diagnostic assertion; leaving surviving peers running for visual verification.\n")
                    << L"Host PID " << host.processId << L", guest PID "
                    << guest.processId << L".\n"
                    << L"State root: " << sessionRoot.wstring() << L"\n";
                return peersSurvived ? 0 : 1;
            }
            const bool guestStopped = CloseCreatedProcess(
                guest.process.get(), guest.processId, guest.shutdownEvent.get());
            const bool hostStopped = CloseCreatedProcess(
                host.process.get(), host.processId, host.shutdownEvent.get());
            if (!peersSurvived || !guestStopped || !hostStopped)
            {
                std::wcerr
                    << (heroWillTest
                        ? L"Multiplayer Hero Will acceptance failed before both peers completed the supported spell sequence.\n"
                        : L"Multiplayer combat authority acceptance failed during the primary-attacker lease handoff.\n");
                return 1;
            }
            std::wcout
                << (heroWillTest
                    ? L"Multiplayer Hero Will acceptance passed in the Chamber of Fate: both Heroes submitted and replayed the complete supported retail Will ability sequence.\n"
                    : L"Multiplayer combat acceptance passed in the Chamber of Fate: both Heroes attacked the replicated enemy, exchanged PvP attacks, kept weapon transitions ordered, converged health, and replayed the complete retail Will ability sequence.\n")
                << L"State root: " << sessionRoot.wstring() << L"\n";
            return 0;
        }
        if (authorityTest)
        {
            const std::uint64_t hostActorId =
                StablePlayerActorId(L"host", L"Host");
            const std::uint64_t guestActorId =
                StablePlayerActorId(L"guest", L"Guest");
            const std::string guestGrant =
                "map=BowerstonePosh authority_actor_id=" +
                std::to_string(guestActorId) + " epoch=2";
            const std::string guestMovementOwner =
                "owner_actor_id=" + std::to_string(guestActorId) +
                " map=BowerstonePosh epoch=2";
            const bool handoffCompleted =
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerTransitionAcceptanceStarted",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerWorldTransitionCompleted",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerMapAuthorityChanged",
                    guestGrant,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerMapAuthorityChanged",
                    guestGrant,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerEntitySimulationCoverage",
                    "local_simulation=22 fenced=0",
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerEntityMovementPublishedMoving",
                    guestMovementOwner,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerEntityMovementAcceptedMoving",
                    guestMovementOwner,
                    timeoutSeconds);
            const bool returnCompleted = handoffCompleted &&
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerTransitionAcceptanceReturned",
                    timeoutSeconds) &&
                WaitForMultiplayerEventCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemoteDefinitionCreated",
                    2,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerRemoteAvatarResumed",
                    "player=Host map=BowerstonePosh action=resumed",
                    timeoutSeconds);
            if (returnCompleted)
            {
                Sleep(2'000);
            }
            std::string hostEventContent = ReadEventFile(hostEvents);
            std::string guestEventContent = ReadEventFile(guestEvents);
            const bool stickyGuestAuthority = returnCompleted &&
                EventDetailContains(
                    hostEventContent,
                    "MultiplayerMapAuthorityChanged",
                    guestGrant.c_str()) &&
                EventDetailContains(
                    guestEventContent,
                    "MultiplayerMapAuthorityChanged",
                    guestGrant.c_str()) &&
                EventDetailCount(
                    hostEventContent,
                    "MultiplayerMapAuthorityChanged",
                    "operation=grant map=BowerstonePosh") == 2 &&
                EventDetailCount(
                    guestEventContent,
                    "MultiplayerMapAuthorityChanged",
                    "operation=grant map=BowerstonePosh") == 2;
            host.window = FindMainWindow(host.processId);
            guest.window = FindMainWindow(guest.processId);
            const bool peersTogether = stickyGuestAuthority &&
                WaitForSingleObject(host.process.get(), 0) == WAIT_TIMEOUT &&
                WaitForSingleObject(guest.process.get(), 0) == WAIT_TIMEOUT &&
                host.window != nullptr && guest.window != nullptr &&
                WindowIsResponsive(host.window) &&
                WindowIsResponsive(guest.window) &&
                !EventWasReported(hostEventContent, "ClientFailed") &&
                !EventWasReported(guestEventContent, "ClientFailed");
            const bool guestStopped = peersTogether && CloseCreatedProcess(
                guest.process.get(),
                guest.processId,
                guest.shutdownEvent.get());
            const std::string hostRecoveryGrant =
                "map=BowerstonePosh authority_actor_id=" +
                std::to_string(hostActorId) + " epoch=3";
            const bool disconnectRecovered = guestStopped &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerMapAuthorityChanged",
                    hostRecoveryGrant,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerEntitySimulationCoverage",
                    "local_simulation=22 fenced=0",
                    timeoutSeconds);
            if (disconnectRecovered)
            {
                Sleep(2'000);
            }
            hostEventContent = ReadEventFile(hostEvents);
            host.window = FindMainWindow(host.processId);
            const bool hostSurvivedRecovery = disconnectRecovered &&
                WaitForSingleObject(host.process.get(), 0) == WAIT_TIMEOUT &&
                host.window != nullptr && WindowIsResponsive(host.window) &&
                !EventWasReported(hostEventContent, "ClientFailed");
            const bool hostStopped = CloseCreatedProcess(
                host.process.get(), host.processId, host.shutdownEvent.get());
            if (!hostSurvivedRecovery || !hostStopped)
            {
                std::wcerr
                    << L"Multiplayer authority acceptance failed during split-map handoff, sticky host re-entry, or owner-disconnect recovery.\n";
                return 1;
            }
            std::wcout
                << L"Multiplayer authority acceptance passed: the host left Bowerstone Posh, atomically granted epoch 2 to the remaining guest, returned without stealing that sticky lease, then recovered all 22 NPC simulations at epoch 3 after the owning guest disconnected.\n"
                << L"State root: " << sessionRoot.wstring() << L"\n";
            return 0;
        }
        if (transitionTest)
        {
            const std::string transferTarget =
                "script_name=SCRIPT_NAME_FABLE_TOGETHER_TRANSFER_TARGET";
            const std::uint64_t hostActorId =
                StablePlayerActorId(L"host", L"Host");
            const std::uint64_t guestActorId =
                StablePlayerActorId(L"guest", L"Guest");
            const std::string destinationGrant =
                "operation=grant map=BowerstoneJail";
            const std::string sourceRelease =
                "operation=release map=BowerstonePosh authority_actor_id=0";
            const bool npcSourceTransferCompleted =
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerNpcTransferTargetSpawned",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerNpcTransferSourceTeardownRequested",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerEntityTransferred",
                    timeoutSeconds);
            const std::size_t hostTransferMaterializationsBeforeArrival =
                EventDetailCount(
                    ReadEventFile(hostEvents),
                    "MultiplayerEntityMaterialized",
                    transferTarget.c_str());
            const std::size_t guestTransferMaterializationsBeforeArrival =
                EventDetailCount(
                    ReadEventFile(guestEvents),
                    "MultiplayerEntityMaterialized",
                    transferTarget.c_str());
            const bool transitionCompleted = npcSourceTransferCompleted &&
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerTransitionAcceptanceStarted",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerWorldTransitionStarted",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerWorldTransitionCompleted",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemoteWorldPresentationQuarantined",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerTransitionAcceptanceStarted",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerWorldTransitionStarted",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerWorldTransitionCompleted",
                    timeoutSeconds) &&
                WaitForMultiplayerEvent(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerRemoteWorldPresentationQuarantined",
                    timeoutSeconds) &&
                WaitForMultiplayerEventCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerRemoteDefinitionCreated",
                    2,
                    timeoutSeconds) &&
                WaitForMultiplayerEventCount(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerRemoteDefinitionCreated",
                    2,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerMapAuthorityChanged",
                    destinationGrant,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerMapAuthorityChanged",
                    destinationGrant,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerMapAuthorityChanged",
                    sourceRelease,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetail(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerMapAuthorityChanged",
                    sourceRelease,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    host,
                    hostEvents,
                    L"host",
                    "MultiplayerEntityMaterialized",
                    transferTarget,
                    hostTransferMaterializationsBeforeArrival + 1,
                    timeoutSeconds) &&
                WaitForMultiplayerEventDetailCount(
                    guest,
                    guestEvents,
                    L"guest",
                    "MultiplayerEntityMaterialized",
                    transferTarget,
                    guestTransferMaterializationsBeforeArrival + 1,
                    timeoutSeconds);
            // The original fault arrived from Fable's asynchronous graphics
            // queue shortly after destination bind. Keep both peers alive for
            // several seconds beyond the lifecycle events before accepting.
            if (transitionCompleted)
            {
                Sleep(8'000);
            }
            const std::string hostEventContent = ReadEventFile(hostEvents);
            const std::string guestEventContent = ReadEventFile(guestEvents);
            const std::string hostDestinationOwner =
                destinationGrant + " authority_actor_id=" +
                std::to_string(hostActorId);
            const std::string guestDestinationOwner =
                destinationGrant + " authority_actor_id=" +
                std::to_string(guestActorId);
            const bool oneAtomicDestinationOwner = transitionCompleted &&
                EventDetailCount(
                    hostEventContent,
                    "MultiplayerMapAuthorityChanged",
                    destinationGrant.c_str()) == 1 &&
                EventDetailCount(
                    guestEventContent,
                    "MultiplayerMapAuthorityChanged",
                    destinationGrant.c_str()) == 1 &&
                ((EventDetailContains(
                        hostEventContent,
                        "MultiplayerMapAuthorityChanged",
                        hostDestinationOwner.c_str()) &&
                    EventDetailContains(
                        guestEventContent,
                        "MultiplayerMapAuthorityChanged",
                        hostDestinationOwner.c_str())) ||
                 (EventDetailContains(
                        hostEventContent,
                        "MultiplayerMapAuthorityChanged",
                        guestDestinationOwner.c_str()) &&
                    EventDetailContains(
                        guestEventContent,
                        "MultiplayerMapAuthorityChanged",
                        guestDestinationOwner.c_str())));
            const bool healthFollowedGeneration =
                EventDetailContains(
                    hostEventContent,
                    "MultiplayerEntityVitalsRestored",
                    transferTarget.c_str()) ||
                EventDetailContains(
                    guestEventContent,
                    "MultiplayerEntityVitalsRestored",
                    transferTarget.c_str());
            host.window = FindMainWindow(host.processId);
            guest.window = FindMainWindow(guest.processId);
            const bool peersSurvived = oneAtomicDestinationOwner &&
                healthFollowedGeneration &&
                WaitForSingleObject(host.process.get(), 0) == WAIT_TIMEOUT &&
                WaitForSingleObject(guest.process.get(), 0) == WAIT_TIMEOUT &&
                host.window != nullptr && guest.window != nullptr &&
                WindowIsResponsive(host.window) &&
                WindowIsResponsive(guest.window) &&
                !EventWasReported(hostEventContent, "ClientFailed") &&
                !EventWasReported(guestEventContent, "ClientFailed");
            const bool guestStopped = CloseCreatedProcess(
                guest.process.get(), guest.processId, guest.shutdownEvent.get());
            const bool hostStopped = CloseCreatedProcess(
                host.process.get(), host.processId, host.shutdownEvent.get());
            if (!peersSurvived || !guestStopped || !hostStopped)
            {
                std::wcerr
                    << L"Multiplayer transition acceptance failed during destination bind, graphics-queue grace, or shutdown.\n";
                return 1;
            }
            std::wcout
                << L"Multiplayer transition acceptance passed: simultaneous boundary requests resolved to one destination owner, one canonical guard retained health while crossing source high-sim through host low-sim, materialized for both destination observers, and both processes remained responsive beyond the former crash windows.\n"
                << L"State root: " << sessionRoot.wstring() << L"\n";
            return 0;
        }
        if (sixPeerShowcase)
        {
            std::wcout
                << L"Manual six-peer Chamber roster showcase is ready. Host PID "
                << host.processId << L", guest PIDs " << guest.processId << L", "
                << guest2.processId << L", " << showcaseGuests[0].processId << L", "
                << showcaseGuests[1].processId << L", " << showcaseGuests[2].processId
                << L". All processes are being left running.\n"
                << L"No synthetic movement, targeting, attacks, spells, or automated combat input was submitted.\n"
                << L"State root: " << sessionRoot.wstring() << L"\n";
            std::wcout
                << L"The six-peer coordinator is staying alive passively; no focus, input, or automatic shutdown will be performed.\n";
            for (;;)
            {
                Sleep(1'000);
            }
        }
        if (interactive)
        {
            std::wcout
                << L"Manual multiplayer playtest is ready. Host PID "
                << host.processId << L", guest PID " << guest.processId
                << L". Both processes are being left running.\n"
                << L"Both native remote characters exist. Use either window normally; the launcher no longer blocks handoff on an arbitrary automated separation distance.\n"
                << L"State root: " << sessionRoot.wstring() << L"\n";
            return 0;
        }
        if (rosterTest)
        {
            const std::uint64_t guestActorId =
                StablePlayerActorId(L"guest", L"Guest");
            const bool guestRelayed = MoveMultiplayerPeer(guest, L"guest") &&
                WaitForBackgroundMovement(
                    host,
                    hostEvents,
                    L"host",
                    guestActorId,
                    timeoutSeconds) &&
                WaitForBackgroundMovement(
                    guest2,
                    guest2Events,
                    L"guest2",
                    guestActorId,
                    timeoutSeconds);
            const bool guest2Stopped = CloseCreatedProcess(
                guest2.process.get(), guest2.processId,
                guest2.shutdownEvent.get());
            const bool guestStopped = CloseCreatedProcess(
                guest.process.get(), guest.processId,
                guest.shutdownEvent.get());
            const bool hostStopped = CloseCreatedProcess(
                host.process.get(), host.processId,
                host.shutdownEvent.get());
            if (!guestRelayed || !guest2Stopped || !guestStopped ||
                !hostStopped)
            {
                std::wcerr
                    << L"Multiplayer roster acceptance failed during guest-to-guest relay or shutdown.\n";
                return 1;
            }
            std::wcout
                << L"Multiplayer roster acceptance passed: host plus two independently owned guests established three actor channels, every process presented both remote actors, and one guest's movement reached both the host and the other unfocused guest through host routing.\n"
                << L"State root: " << sessionRoot.wstring() << L"\n";
            return 0;
        }
        // Keep the observer unfocused while its owner moves. UE3 may throttle
        // that process's animation worker, so the background gate is native
        // physics displacement; animation is asserted independently after a
        // peer owns the foreground again.
        const std::uint64_t hostActorId = StablePlayerActorId(L"host", L"Host");
        const std::uint64_t guestActorId = StablePlayerActorId(L"guest", L"Guest");
        const bool hostMoved = MoveMultiplayerPeer(host, L"host") &&
            WaitForBackgroundMovement(
                guest, guestEvents, L"guest", hostActorId, timeoutSeconds);
        const bool guestMoved = hostMoved && MoveMultiplayerPeer(guest, L"guest") &&
            WaitForBackgroundMovement(
                host, hostEvents, L"host", guestActorId, timeoutSeconds);
        const bool nativeAnimationApplied = guestMoved &&
            (EventWasReported(
                 ReadEventFile(hostEvents),
                 "MultiplayerRemoteAvatarWalking") ||
                EventWasReported(
                    ReadEventFile(guestEvents),
                    "MultiplayerRemoteAvatarWalking"));
        const bool appearancesApplied = nativeAnimationApplied &&
            WaitForMultiplayerEvent(
                host,
                hostEvents,
                L"host",
                "MultiplayerRemoteAppearanceModifiersApplied",
                timeoutSeconds) &&
            WaitForMultiplayerEvent(
                guest,
                guestEvents,
                L"guest",
                "MultiplayerRemoteAppearanceModifiersApplied",
                timeoutSeconds) &&
            WaitForMultiplayerEvent(
                host,
                hostEvents,
                L"host",
                "MultiplayerRemoteBoneScalesApplied",
                timeoutSeconds) &&
            WaitForMultiplayerEvent(
                guest,
                guestEvents,
                L"guest",
                "MultiplayerRemoteBoneScalesApplied",
                timeoutSeconds);
        const bool guestStopped = CloseCreatedProcess(
            guest.process.get(), guest.processId, guest.shutdownEvent.get());
        const bool hostStopped = CloseCreatedProcess(
            host.process.get(), host.processId, host.shutdownEvent.get());
        if (!hostMoved || !guestMoved || !nativeAnimationApplied ||
            !appearancesApplied ||
            !guestStopped || !hostStopped)
        {
            std::wcerr << L"Multiplayer acceptance failed during movement or shutdown.\n";
            return 1;
        }

        std::wcout
            << L"Multiplayer structural acceptance passed: both remote definitions were created from the safe dispatch context, each unfocused peer applied non-zero remote physics movement without focus handoff, the shared focused native-animation path remained live, and each reconciled selected-save appearance. This command does not claim pixel-level visibility.\n"
            << L"State root: " << sessionRoot.wstring() << L"\n";
        return 0;
    }

    int RunDualInstanceTest(
        const fs::path& executable,
        const fs::path& clientDll,
        const fs::path& sessionRoot,
        const std::wstring& sessionId,
        unsigned int timeoutSeconds,
        unsigned int holdSeconds,
        const std::vector<std::wstring>& originalArguments)
    {
        if (AnyFableProcessIsRunning())
        {
            std::wcerr << L"Dual-instance test refused because a pre-existing Fable Anniversary process is running.\n";
            return 1;
        }

        const std::vector<std::wstring> arguments =
            LocalWindowArguments(originalArguments);
        const auto roleRoot = [&](const wchar_t* role)
        {
            return sessionRoot / role;
        };
        const auto prepareRole = [&](const wchar_t* role) -> bool
        {
            std::error_code error;
            fs::create_directories(roleRoot(role) / L"Documents", error);
            if (!error)
            {
                fs::create_directories(roleRoot(role) / L"script-data", error);
            }
            return !error;
        };
        if (!prepareRole(L"host") || !prepareRole(L"guest"))
        {
            std::wcerr << L"Could not create isolated host and guest state roots.\n";
            return 1;
        }

        LaunchedGame host;
        LaunchedGame guest;
        const auto spawnRole = [&](const wchar_t* role, LaunchedGame& game) -> bool
        {
            const fs::path root = roleRoot(role);
            const std::wstring runId = sessionId + L"-" + role;
            return SpawnGame(
                executable,
                clientDll,
                root / L"client.log",
                root / L"events.jsonl",
                root / L"Documents",
                {},
                root / L"script-data",
                L"observe",
                L"dual_title_screen",
                runId,
                sessionId,
                role,
                {},
                {},
                0,
                {},
                {},
                arguments,
                game);
        };
        const auto stop = [](LaunchedGame& game)
        {
            if (game.process.valid())
            {
                CloseCreatedProcess(
                    game.process.get(),
                    game.processId,
                    game.shutdownEvent.get());
            }
        };

        std::wcout << L"Dual test: starting isolated host first.\n";
        if (!spawnRole(L"host", host) ||
            !WaitForLocalInstanceReady(
                host,
                roleRoot(L"host") / L"events.jsonl",
                L"host",
                0,
                timeoutSeconds))
        {
            stop(host);
            return 1;
        }

        std::wcout << L"Dual test: host is responsive; starting isolated guest.\n";
        if (!spawnRole(L"guest", guest) ||
            !WaitForLocalInstanceReady(
                guest,
                roleRoot(L"guest") / L"events.jsonl",
                L"guest",
                kLocalTestWindowPitch,
                timeoutSeconds))
        {
            stop(guest);
            stop(host);
            return 1;
        }

        if (host.processId == guest.processId || host.window == guest.window)
        {
            std::wcerr << L"Dual test failed: host and guest did not receive distinct PID and HWND identities.\n";
            stop(guest);
            stop(host);
            return 1;
        }

        std::wcout << L"Dual test: both compact side-by-side title windows are responsive; observing "
                   << holdSeconds << L" seconds of coexistence.\n";
        const ULONGLONG holdDeadline = GetTickCount64() +
            static_cast<ULONGLONG>(holdSeconds) * 1'000;
        while (GetTickCount64() < holdDeadline)
        {
            const bool processesAlive =
                WaitForSingleObject(host.process.get(), 0) == WAIT_TIMEOUT &&
                WaitForSingleObject(guest.process.get(), 0) == WAIT_TIMEOUT;
            const bool hooksHealthy =
                !EventWasReported(
                    ReadEventFile(roleRoot(L"host") / L"events.jsonl"),
                    "ClientFailed") &&
                !EventWasReported(
                    ReadEventFile(roleRoot(L"guest") / L"events.jsonl"),
                    "ClientFailed");
            if (!processesAlive || !hooksHealthy ||
                !WindowIsResponsive(host.window) ||
                !WindowIsResponsive(guest.window))
            {
                std::wcerr << L"Dual test failed during the coexistence interval.\n";
                stop(guest);
                stop(host);
                return 1;
            }
            Sleep(500);
        }

        const bool finalGeometry =
            PositionLocalWindow(host.window, L"host", 0, 0) &&
            PositionLocalWindow(
                guest.window, L"guest", kLocalTestWindowPitch, 0);
        const bool guestStopped = CloseCreatedProcess(
            guest.process.get(),
            guest.processId,
            guest.shutdownEvent.get());
        const bool hostStopped = CloseCreatedProcess(
            host.process.get(),
            host.processId,
            host.shutdownEvent.get());
        if (!finalGeometry || !guestStopped || !hostStopped)
        {
            std::wcerr << L"Dual test failed during final geometry validation or shutdown.\n";
            return 1;
        }

        std::wcout << L"Dual-instance acceptance passed: distinct responsive host and guest PIDs/HWNDs coexisted in isolated compact side-by-side windows.\n"
                   << L"State root: " << sessionRoot.wstring() << L"\n";
        return 0;
    }

    int Launch(
        const fs::path& executable,
        const fs::path& clientDll,
        const fs::path& clientLog,
        const fs::path& eventPath,
        const fs::path& fixtureDocuments,
        const fs::path& characterSnapshot,
        const fs::path& scriptData,
        const std::wstring& clientMode,
        const std::wstring& scenario,
        const std::wstring& runId,
        const std::wstring& localSession,
        const std::wstring& localInstance,
        const std::wstring& multiplayerRole,
        const std::wstring& multiplayerAddress,
        unsigned short multiplayerPort,
        const std::wstring& multiplayerPlayerId,
        const std::wstring& multiplayerAppearance,
        unsigned int automationTimeoutSeconds,
        const std::vector<std::wstring>& arguments)
    {
        LaunchedGame launched;
        if (!SpawnGame(
                executable,
                clientDll,
                clientLog,
                eventPath,
                fixtureDocuments,
                characterSnapshot,
                scriptData,
                clientMode,
                scenario,
                runId,
                localSession,
                localInstance,
                multiplayerRole,
                multiplayerAddress,
                multiplayerPort,
                multiplayerPlayerId,
                multiplayerAppearance,
                arguments,
                launched))
        {
            return 1;
        }
        if (!scenario.empty())
        {
            return RunAutomation(
                launched.process.get(),
                launched.processId,
                eventPath,
                scenario,
                automationTimeoutSeconds,
                launched.shutdownEvent.get(),
                !characterSnapshot.empty());
        }
        if (!localInstance.empty())
        {
            const int x = localInstance == L"host"
                ? 0
                : kLocalTestWindowPitch;
            if (!WaitForLocalInstanceReady(
                    launched,
                    eventPath,
                    localInstance.c_str(),
                    x,
                    automationTimeoutSeconds))
            {
                CloseCreatedProcess(
                    launched.process.get(),
                    launched.processId,
                    nullptr);
                return 1;
            }
            std::wcout << L"Local " << localInstance
                       << L" is ready in a compact test window; launcher is leaving PID "
                       << launched.processId << L" running.\n";
        }
        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    Options options;
    std::wstring optionError;
    if (!ParseOptions(argc, argv, options, optionError))
    {
        std::wcerr << optionError << L"\n\n";
        PrintUsage();
        return 2;
    }
    if (options.showHelp)
    {
        PrintUsage();
        return 0;
    }

    const fs::path launcherDirectory = GetLauncherDirectory();
    const fs::path executable = ResolveExecutable(options, launcherDirectory);
    const fs::path clientDll = options.clientDll.empty()
        ? AbsolutePath(launcherDirectory / kClientDllName)
        : AbsolutePath(options.clientDll);
    const std::wstring runId = CreateRunId();
    const bool localSingleInstance = !options.localInstance.empty();
    const std::wstring localSession = localSingleInstance
        ? options.localSession.empty() ? runId : options.localSession
        : std::wstring();
    const std::wstring artifactId = localSingleInstance ? localSession : runId;
    const fs::path artifactRoot = AbsolutePath(
        clientDll.parent_path() / L"artifacts" / artifactId);
    const fs::path instanceRoot = localSingleInstance
        ? artifactRoot / options.localInstance
        : artifactRoot;
    const fs::path eventPath = instanceRoot / L"events.jsonl";
    const fs::path clientLog = instanceRoot / L"client.log";
    const fs::path characterSnapshotSource = options.characterSnapshot.empty()
        ? fs::path()
        : AbsolutePath(options.characterSnapshot);
    const fs::path characterSnapshot = characterSnapshotSource.empty()
        ? fs::path()
        : eventPath.parent_path() / L"character-snapshot.json";
    const bool loadFixtureScenario =
        options.automationScenario == L"load_fixture" ||
        options.automationScenario == L"appearance_cycle" ||
        options.multiplayerTest || options.multiplayerRosterTest ||
        options.multiplayerTransitionTest ||
        options.multiplayerAuthorityTest ||
        options.multiplayerCombatTest || options.multiplayerHeroWillTest ||
        options.multiplayerPlaytest;
    const fs::path fixtureDocumentsSource = loadFixtureScenario
        ? options.fixtureDocuments.empty()
            ? ResolveDeploymentAsset(
                launcherDirectory,
                fs::path(L"fixtures") /
                    ((options.multiplayerCombatTest || options.multiplayerHeroWillTest ||
                        (options.multiplayerRosterTest && options.multiplayerPlaytest))
                        ? L"combat-chamber-hero3"
                        : L"adult-town") /
                    L"Documents",
                true)
            : AbsolutePath(options.fixtureDocuments)
        : fs::path();
    const fs::path defaultFixtureDocuments =
        options.automationScenario == L"bootstrap_fixture_probe"
        ? clientDll.parent_path() / L"fixtures" / L"bootstrap" / runId / L"Documents"
        : loadFixtureScenario
        ? clientDll.parent_path() / L"fixtures" / L"load" / runId / L"Documents"
        : clientDll.parent_path() / L"fixtures" / L"automation" / L"Documents";
    const fs::path fixtureDocuments = localSingleInstance
        ? instanceRoot / L"Documents"
        : options.automationScenario.empty()
        ? fs::path()
        : AbsolutePath(
            options.fixtureDocuments.empty() || loadFixtureScenario
                ? defaultFixtureDocuments
                : options.fixtureDocuments);
    const fs::path scriptData = localSingleInstance
        ? instanceRoot / L"script-data"
        : fs::path();
    const std::wstring clientMode = options.transformationProbe
        ? L"transform_probe"
        : !options.multiplayerRole.empty()
            ? L"observe"
        : localSingleInstance || options.dualInstanceTest
            ? L"observe"
        : options.automationScenario.empty()
            ? L"appearance_cycle"
            : L"observe";

    std::wcout << L"Game:   " << (executable.empty() ? L"<not found>" : executable.wstring()) << L'\n'
               << L"Client: " << clientDll.wstring() << L'\n'
               << L"Mode:   " << clientMode << L'\n'
               << L"Run:    " << runId << L'\n';
    if (!options.dualInstanceTest && !options.multiplayerTest &&
        !options.multiplayerRosterTest &&
        !options.multiplayerTransitionTest &&
        !options.multiplayerAuthorityTest &&
        !options.multiplayerCombatTest && !options.multiplayerHeroWillTest &&
        !options.multiplayerPlaytest)
    {
        std::wcout << L"Log:    " << clientLog.wstring() << L'\n'
                   << L"Events: " << eventPath.wstring() << L'\n';
    }
    if (localSingleInstance)
    {
        std::wcout << L"Local:  session=" << localSession
                   << L" instance=" << options.localInstance << L'\n'
                   << L"Documents: " << fixtureDocuments.wstring() << L'\n'
                   << L"Script data: " << scriptData.wstring() << L'\n';
    }
    if (!options.multiplayerRole.empty())
    {
        std::wcout << L"Multiplayer: role=" << options.multiplayerRole
                   << L" port=" << options.multiplayerPort
                   << L" player=" << options.multiplayerPlayerId
                   << L" appearance=" << options.multiplayerAppearance << L'\n';
        if (options.multiplayerRole == L"guest")
        {
            std::wcout << L"Host:   " << options.multiplayerAddress << L'\n';
        }
    }
    if (options.dualInstanceTest)
    {
        std::wcout << L"Test:   dual_instance_title_screen\n"
                   << L"State:  " << artifactRoot.wstring() << L'\n';
    }
    if (options.multiplayerTest || options.multiplayerRosterTest ||
        options.multiplayerTransitionTest ||
        options.multiplayerAuthorityTest ||
        options.multiplayerCombatTest || options.multiplayerHeroWillTest ||
        options.multiplayerPlaytest)
    {
        std::wcout << L"Test:   "
                    << (options.multiplayerPlaytest
                        ? options.multiplayerCombatTest
                            ? L"multiplayer_combat_manual"
                            : options.multiplayerRosterTest
                                ? L"multiplayer_chamber_roster_manual"
                                : L"multiplayer_adult_town_manual"
                       : options.multiplayerHeroWillTest
                           ? L"multiplayer_hero_will"
                       : options.multiplayerCombatTest
                           ? L"multiplayer_combat_authority_handoff"
                       : options.multiplayerAuthorityTest
                           ? L"multiplayer_map_authority_handoff"
                       : options.multiplayerTransitionTest
                           ? L"multiplayer_map_transition"
                       : options.multiplayerRosterTest
                           ? L"multiplayer_three_peer_roster"
                       : L"multiplayer_adult_town") << L"\n"
                   << L"Fixture Source: " << fixtureDocumentsSource.wstring() << L'\n'
                   << L"State:  " << artifactRoot.wstring() << L'\n';
    }
    if (!options.automationScenario.empty())
    {
        std::wcout << L"Test:   " << options.automationScenario << L'\n';
        if (loadFixtureScenario)
        {
            std::wcout << L"Fixture Source: " << fixtureDocumentsSource.wstring() << L'\n';
        }
        std::wcout << L"Fixture Documents: " << fixtureDocuments.wstring() << L'\n';
        if (!characterSnapshotSource.empty())
        {
            std::wcout << L"Character Snapshot Source: "
                       << characterSnapshotSource.wstring() << L'\n';
            std::wcout << L"Character Snapshot: "
                       << characterSnapshot.wstring() << L'\n';
        }
    }

    if (executable.empty() || !IsFile(executable))
    {
        std::wcerr << L"Fable Anniversary.exe was not found. Use --game-dir or --exe.\n";
        return 1;
    }
    if (!IsFile(clientDll))
    {
        std::wcerr << L"FableTogether.Client.dll was not found beside the launcher. Use --dll to override.\n";
        return 1;
    }
    if (!characterSnapshotSource.empty() && !IsFile(characterSnapshotSource))
    {
        std::wcerr << L"The character snapshot is not an existing file.\n";
        return 1;
    }
    if (loadFixtureScenario)
    {
        const fs::path ordinaryDocuments = GetOrdinaryDocumentsPath();
        if (!IsDirectory(fixtureDocumentsSource))
        {
            std::wcerr << L"The load fixture source is not an existing directory.\n";
            return 1;
        }
        if (!ordinaryDocuments.empty() &&
            IsSamePathOrBelow(fixtureDocumentsSource, ordinaryDocuments))
        {
            std::wcerr << L"Refusing to load a fixture from the ordinary Documents tree.\n";
            return 1;
        }

        const fs::path saveRoot = fixtureDocumentsSource /
            L"My Games" / L"FableHD" / L"Saves" /
            ((options.multiplayerCombatTest || options.multiplayerHeroWillTest ||
                (options.multiplayerRosterTest && options.multiplayerPlaytest))
                ? L"Hero3" : L"Hero1");
        if (!IsFile(saveRoot / L"Profile.bin") ||
            !IsFile(saveRoot / L"AutoSave"))
        {
            std::wcerr << L"The fixture must contain an isolated Profile.bin and AutoSave pair.\n";
            return 1;
        }
    }
    if (options.dryRun)
    {
        std::wcout << L"Dry run succeeded; no process was started.\n";
        return 0;
    }

    if (options.dualInstanceTest)
    {
        return RunDualInstanceTest(
            executable,
            clientDll,
            artifactRoot,
            runId,
            options.automationTimeoutSeconds,
            options.dualInstanceHoldSeconds,
            options.gameArguments);
    }
    if (options.multiplayerTest || options.multiplayerRosterTest ||
        options.multiplayerTransitionTest ||
        options.multiplayerAuthorityTest ||
        options.multiplayerCombatTest || options.multiplayerHeroWillTest ||
        options.multiplayerPlaytest)
    {
        return RunMultiplayerTest(
            executable,
            clientDll,
            fixtureDocumentsSource,
            artifactRoot,
            runId,
            options.multiplayerPort,
            options.automationTimeoutSeconds,
            options.multiplayerPlaytest,
            options.multiplayerRosterTest,
            options.multiplayerTransitionTest,
            options.multiplayerAuthorityTest,
            options.multiplayerCombatTest,
            options.multiplayerHeroWillTest,
            options.gameArguments);
    }

    std::error_code artifactError;
    fs::create_directories(eventPath.parent_path(), artifactError);
    if (artifactError)
    {
        std::wcerr << L"Could not create the run artifact directory: "
                   << artifactError.message().c_str() << L'\n';
        return 1;
    }
    if (!characterSnapshotSource.empty())
    {
        fs::copy_file(
            characterSnapshotSource,
            characterSnapshot,
            fs::copy_options::overwrite_existing,
            artifactError);
        if (artifactError)
        {
            std::wcerr << L"Could not copy the character snapshot into the immutable run artifacts: "
                       << artifactError.message().c_str() << L'\n';
            return 1;
        }
    }
    if (!fixtureDocuments.empty())
    {
        std::error_code fixtureError;
        fs::create_directories(fixtureDocuments, fixtureError);
        if (fixtureError)
        {
            std::wcerr << L"Could not create the isolated fixture Documents directory: "
                       << fixtureError.message().c_str() << L'\n';
            return 1;
        }
        if (loadFixtureScenario)
        {
            fs::copy(
                fixtureDocumentsSource,
                fixtureDocuments,
                fs::copy_options::recursive |
                    fs::copy_options::overwrite_existing,
                fixtureError);
            if (fixtureError)
            {
                std::wcerr << L"Could not copy the isolated fixture into its run-specific working directory: "
                           << fixtureError.message().c_str() << L'\n';
                return 1;
            }
        }
    }
    if (!scriptData.empty())
    {
        std::error_code storageError;
        fs::create_directories(scriptData, storageError);
        if (storageError)
        {
            std::wcerr << L"Could not create the isolated script-data directory: "
                       << storageError.message().c_str() << L'\n';
            return 1;
        }
    }

    std::vector<std::wstring> gameArguments = options.gameArguments;
    if (!options.automationScenario.empty())
    {
        const bool alreadySkipsMovies = std::any_of(
            gameArguments.begin(),
            gameArguments.end(),
            [](const std::wstring& argument)
            {
                return _wcsicmp(argument.c_str(), L"-nomoviestartup") == 0;
            });
        if (!alreadySkipsMovies)
        {
            gameArguments.emplace_back(L"-nomoviestartup");
        }
    }
    if (localSingleInstance)
    {
        gameArguments = LocalWindowArguments(gameArguments);
    }

    return Launch(
        executable,
        clientDll,
        clientLog,
        eventPath,
        fixtureDocuments,
        characterSnapshot,
        scriptData,
        clientMode,
        options.automationScenario,
        runId,
        localSession,
        options.localInstance,
        options.multiplayerRole,
        options.multiplayerAddress,
        options.multiplayerPort,
        options.multiplayerPlayerId,
        options.multiplayerAppearance,
        options.automationTimeoutSeconds,
        gameArguments);
}
