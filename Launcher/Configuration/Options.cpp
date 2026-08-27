#include "Options.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <iostream>

namespace fable::launcher
{
namespace
{
    enum class ParseResult
    {
        NotHandled,
        Handled,
        Error
    };

    bool ReadValue(
        int& index,
        int argc,
        wchar_t** argv,
        const wchar_t* missing,
        std::wstring& value,
        std::wstring& error)
    {
        if (++index >= argc)
        {
            error = missing;
            return false;
        }
        value = argv[index];
        return true;
    }

    bool ReadPath(
        int& index,
        int argc,
        wchar_t** argv,
        const std::wstring& option,
        fs::path& destination,
        std::wstring& error)
    {
        std::wstring value;
        if (!ReadValue(
                index,
                argc,
                argv,
                (L"Missing value after " + option).c_str(),
                value,
                error))
        {
            return false;
        }
        destination = value;
        return true;
    }

    bool ReadNumber(
        int& index,
        int argc,
        wchar_t** argv,
        const wchar_t* missing,
        unsigned long minimum,
        unsigned long maximum,
        const wchar_t* rangeError,
        unsigned long& result,
        std::wstring& error)
    {
        std::wstring value;
        if (!ReadValue(index, argc, argv, missing, value, error))
        {
            return false;
        }
        wchar_t* end = nullptr;
        result = std::wcstoul(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != L'\0' ||
            result < minimum || result > maximum)
        {
            error = rangeError;
            return false;
        }
        return true;
    }

    ParseResult ParseFlag(
        const std::wstring& argument,
        Options& options,
        std::wstring& error)
    {
        if (argument == L"--help" || argument == L"-h")
        {
            options.showHelp = true;
        }
        else if (argument == L"--dry-run")
        {
            options.dryRun = true;
        }
        else if (argument == L"--no-console")
        {
            options.showConsole = false;
        }
        else if (argument == L"--no-logs")
        {
            options.generateLogs = false;
        }
        else if (argument == L"--dual-instance-test")
        {
            options.dualInstanceTest = true;
        }
        else if (argument == L"--multiplayer-test")
        {
            options.multiplayerTest = true;
        }
        else if (argument == L"--multiplayer-roster-test")
        {
            options.multiplayerRosterTest = true;
        }
        else if (argument == L"--multiplayer-transition-test")
        {
            options.multiplayerTransitionTest = true;
        }
        else if (argument == L"--multiplayer-authority-test")
        {
            options.multiplayerAuthorityTest = true;
        }
        else if (argument == L"--multiplayer-combat-test")
        {
            options.multiplayerCombatTest = true;
        }
        else if (argument == L"--multiplayer-hero-will-test")
        {
            options.multiplayerHeroWillTest = true;
        }
        else if (argument == L"--multiplayer-playtest")
        {
            options.multiplayerPlaytest = true;
        }
        else if (argument == L"--host")
        {
            if (!options.multiplayerRole.empty())
            {
                error = L"Choose either --host or --join, not both";
                return ParseResult::Error;
            }
            options.multiplayerRole = L"host";
        }
        else if (argument == L"--transform-probe")
        {
            if (!options.automationScenario.empty())
            {
                error = L"--transform-probe cannot be combined with --automation";
                return ParseResult::Error;
            }
            options.transformationProbe = true;
        }
        else
        {
            return ParseResult::NotHandled;
        }
        return ParseResult::Handled;
    }

    ParseResult ParsePathValue(
        int& index,
        int argc,
        wchar_t** argv,
        const std::wstring& argument,
        Options& options,
        std::wstring& error)
    {
        fs::path* destination = nullptr;
        if (argument == L"--exe") destination = &options.executable;
        else if (argument == L"--game-dir") destination = &options.gameDirectory;
        else if (argument == L"--dll") destination = &options.clientDll;
        else if (argument == L"--fixture-documents") destination = &options.fixtureDocuments;
        else if (argument == L"--character-snapshot") destination = &options.characterSnapshot;
        if (destination == nullptr)
        {
            return ParseResult::NotHandled;
        }
        return ReadPath(index, argc, argv, argument, *destination, error)
            ? ParseResult::Handled : ParseResult::Error;
    }

    ParseResult ParseIdentityValue(
        int& index,
        int argc,
        wchar_t** argv,
        const std::wstring& argument,
        Options& options,
        std::wstring& error)
    {
        if (argument == L"--local-instance")
        {
            return ReadValue(index, argc, argv,
                L"Missing identifier after --local-instance",
                options.localInstance, error)
                ? ParseResult::Handled : ParseResult::Error;
        }
        if (argument == L"--local-session")
        {
            return ReadValue(index, argc, argv,
                L"Missing identifier after --local-session",
                options.localSession, error)
                ? ParseResult::Handled : ParseResult::Error;
        }
        if (argument == L"--join")
        {
            if (!options.multiplayerRole.empty())
            {
                error = L"Choose either --host or --join, not both";
                return ParseResult::Error;
            }
            if (!ReadValue(index, argc, argv,
                    L"Missing IPv4 host address after --join",
                    options.multiplayerAddress, error))
            {
                return ParseResult::Error;
            }
            options.multiplayerRole = L"guest";
            return ParseResult::Handled;
        }
        if (argument != L"--player-id" && argument != L"--appearance")
        {
            return ParseResult::NotHandled;
        }
        std::wstring value;
        const wchar_t* missing = argument == L"--player-id"
            ? L"Missing player identifier after --player-id"
            : L"Missing creature definition after --appearance";
        if (!ReadValue(index, argc, argv, missing, value, error) || value.empty())
        {
            if (error.empty()) error = missing;
            return ParseResult::Error;
        }
        if (argument == L"--player-id") options.multiplayerPlayerId = value;
        else options.multiplayerAppearance = value;
        return ParseResult::Handled;
    }

    ParseResult ParseNumericValue(
        int& index,
        int argc,
        wchar_t** argv,
        const std::wstring& argument,
        Options& options,
        std::wstring& error)
    {
        if (argument != L"--port" && argument != L"--hold" &&
            argument != L"--timeout")
        {
            return ParseResult::NotHandled;
        }
        const unsigned long minimum = argument == L"--port" ? 1 :
            argument == L"--hold" ? 5 : 10;
        const unsigned long maximum = argument == L"--port" ? 65'535 :
            argument == L"--hold" ? 300 : 600;
        const wchar_t* missing = argument == L"--port"
            ? L"Missing UDP port after --port"
            : argument == L"--hold"
                ? L"Missing seconds after --hold"
                : L"Missing seconds after --timeout";
        const wchar_t* rangeError = argument == L"--port"
            ? L"--port must be between 1 and 65535"
            : argument == L"--hold"
                ? L"--hold must be between 5 and 300 seconds"
                : L"--timeout must be between 10 and 600 seconds";
        unsigned long value = 0;
        if (!ReadNumber(index, argc, argv, missing, minimum, maximum,
                rangeError, value, error))
        {
            return ParseResult::Error;
        }
        if (argument == L"--port") options.multiplayerPort =
            static_cast<unsigned short>(value);
        else if (argument == L"--hold") options.dualInstanceHoldSeconds =
            static_cast<unsigned int>(value);
        else options.automationTimeoutSeconds = static_cast<unsigned int>(value);
        return ParseResult::Handled;
    }

    ParseResult ParseAutomationValue(
        int& index,
        int argc,
        wchar_t** argv,
        const std::wstring& argument,
        Options& options,
        std::wstring& error)
    {
        if (argument != L"--automation") return ParseResult::NotHandled;
        if (options.transformationProbe)
        {
            error = L"--automation cannot be combined with --transform-probe";
            return ParseResult::Error;
        }
        if (!ReadValue(index, argc, argv,
                L"Missing scenario name after --automation",
                options.automationScenario, error))
        {
            return ParseResult::Error;
        }
        if (options.automationScenario.empty())
        {
            error = L"Automation scenario cannot be empty";
            return ParseResult::Error;
        }
        return ParseResult::Handled;
    }

    ParseResult ParseValue(
        int& index,
        int argc,
        wchar_t** argv,
        const std::wstring& argument,
        Options& options,
        std::wstring& error)
    {
        ParseResult result = ParsePathValue(
            index, argc, argv, argument, options, error);
        if (result != ParseResult::NotHandled) return result;
        result = ParseIdentityValue(index, argc, argv, argument, options, error);
        if (result != ParseResult::NotHandled) return result;
        result = ParseNumericValue(index, argc, argv, argument, options, error);
        if (result != ParseResult::NotHandled) return result;
        return ParseAutomationValue(index, argc, argv, argument, options, error);
    }

    bool ValidIdentifier(
        const std::wstring& value,
        std::size_t maximum,
        bool allowDot)
    {
        if (value.size() > maximum)
        {
            return false;
        }
        return std::all_of(
            value.begin(),
            value.end(),
            [allowDot](wchar_t character)
            {
                return std::iswalnum(character) != 0 ||
                    character == L'-' ||
                    character == L'_' ||
                    (allowDot && character == L'.');
            });
    }

    bool ValidateModes(const Options& options, std::wstring& error)
    {
        const int multiplayerModes =
            (options.multiplayerTest ? 1 : 0) +
            (options.multiplayerRosterTest ? 1 : 0) +
            (options.multiplayerTransitionTest ? 1 : 0) +
            (options.multiplayerAuthorityTest ? 1 : 0) +
            (options.multiplayerCombatTest ? 1 : 0) +
            (options.multiplayerHeroWillTest ? 1 : 0) +
            (options.multiplayerPlaytest ? 1 : 0);
        const bool interactiveRoster =
            options.multiplayerRosterTest &&
            options.multiplayerPlaytest &&
            multiplayerModes == 2;
        const bool interactiveCombat =
            options.multiplayerCombatTest &&
            options.multiplayerPlaytest &&
            multiplayerModes == 2;
        if (multiplayerModes > 1 && !interactiveRoster && !interactiveCombat)
        {
            error = L"Choose one multiplayer test or playtest mode";
            return false;
        }
        return true;
    }

    bool ValidateScenario(const Options& options, std::wstring& error)
    {
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
        return true;
    }

    bool ValidateLocalIdentity(const Options& options, std::wstring& error)
    {
        if (!options.localInstance.empty() &&
            !ValidIdentifier(options.localInstance, 32, false))
        {
            error = L"--local-instance accepts at most 32 letters, digits, hyphens, or underscores";
            return false;
        }
        if (!options.localSession.empty() && options.localInstance.empty())
        {
            error = L"--local-session requires --local-instance";
            return false;
        }
        if (!options.localSession.empty() &&
            !ValidIdentifier(options.localSession, 64, true))
        {
            error = L"--local-session accepts at most 64 letters, digits, dots, hyphens, or underscores";
            return false;
        }
        return true;
    }

    bool ValidateExclusiveModes(const Options& options, std::wstring& error)
    {
        if (options.dualInstanceTest &&
            (!options.automationScenario.empty() ||
                options.transformationProbe ||
                !options.localInstance.empty()))
        {
            error = L"--dual-instance-test cannot be combined with automation, transform-probe, or --local-instance";
            return false;
        }
        const bool multiplayerSelected =
            options.multiplayerTest ||
            options.multiplayerRosterTest ||
            options.multiplayerTransitionTest ||
            options.multiplayerAuthorityTest ||
            options.multiplayerCombatTest ||
            options.multiplayerHeroWillTest ||
            options.multiplayerPlaytest;
        if (multiplayerSelected &&
            (options.dualInstanceTest ||
                !options.multiplayerRole.empty() ||
                !options.automationScenario.empty() ||
                options.transformationProbe ||
                !options.localInstance.empty()))
        {
            error = L"multiplayer test/playtest cannot be combined with another launch mode";
            return false;
        }
        return ValidateModes(options, error);
    }

    void ApplyMultiplayerDefaults(Options& options)
    {
        if (options.multiplayerRole.empty()) return;
        if (options.multiplayerPlayerId.empty())
        {
            options.multiplayerPlayerId =
                options.multiplayerRole == L"host" ? L"Host" : L"Guest";
        }
        if (options.multiplayerAppearance.empty())
        {
            options.multiplayerAppearance = kRemoteHeroDefinition;
        }
    }

    bool ValidateFixtureConstraints(
        const Options& options,
        std::wstring& error)
    {
        if (!options.localInstance.empty() &&
            (!options.automationScenario.empty() ||
                options.transformationProbe))
        {
            error = L"--local-instance cannot be combined with automation or transform-probe";
            return false;
        }
        const bool loadsFixture =
            options.automationScenario == L"load_fixture" ||
            options.automationScenario == L"appearance_cycle";
        if (!options.characterSnapshot.empty() && !loadsFixture)
        {
            error = L"--character-snapshot is supported only with fixture-loading automation";
            return false;
        }
        return true;
    }

    bool ValidateOptions(Options& options, std::wstring& error)
    {
        if (!ValidateScenario(options, error) ||
            !ValidateLocalIdentity(options, error) ||
            !ValidateExclusiveModes(options, error))
        {
            return false;
        }
        if (!options.multiplayerRole.empty() &&
            (!options.automationScenario.empty() ||
                options.transformationProbe))
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
        ApplyMultiplayerDefaults(options);
        return ValidateFixtureConstraints(options, error);
    }
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
        const ParseResult flag = ParseFlag(argument, options, error);
        if (flag == ParseResult::Error)
        {
            return false;
        }
        if (flag == ParseResult::Handled)
        {
            continue;
        }
        const ParseResult value =
            ParseValue(index, argc, argv, argument, options, error);
        if (value == ParseResult::Error)
        {
            return false;
        }
        if (value == ParseResult::NotHandled)
        {
            error = L"Unknown option: " + argument +
                L" (use -- before game arguments)";
            return false;
        }
    }
    return ValidateOptions(options, error);
}

void PrintUsage()
{
    std::wcout
        << L"AlbionTogether.Launcher [options] [-- game arguments]\n\n"
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
        << L"  --no-console       Do not open the injected client diagnostics console\n"
        << L"  --no-logs          Do not create client or event log files\n"
        << L"  --help, -h         Show this help\n";
}
}
