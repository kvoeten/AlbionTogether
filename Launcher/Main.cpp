#include "Configuration/LauncherConstants.h"
#include "Configuration/Options.h"
#include "Configuration/Paths.h"
#include "Automation/DualInstanceScenario.h"
#include "Application/LaunchPlan.h"
#include "Application/SingleGameLaunch.h"
#include "Multiplayer/Scenarios/ScenarioRunners.h"
#include "Diagnostics/FirewallManager.h"
#include "Platform/CommandLineStreams.h"
#include "UI/LauncherWindow.h"

#include <filesystem>
#include <cwchar>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    using fable::launcher::automation::RunDualInstanceTest;
    using fable::launcher::application::BuildLaunchPlan;
    using fable::launcher::application::LaunchMode;
    using fable::launcher::application::PrepareLaunchArtifacts;
    using fable::launcher::application::PrintPlanSummary;
    using fable::launcher::application::RunSingleGame;
    using fable::launcher::application::ValidateLaunchPlan;
    using fable::launcher::multiplayer::MultiplayerScenario;
    using fable::launcher::multiplayer::MultiplayerTestContext;
    using fable::launcher::multiplayer::RunMultiplayerTest;

    static_assert(sizeof(void*) == 4, "The launcher must match Fable Anniversary's x86 process.");
    using fable::launcher::GetLauncherDirectory;
    using fable::launcher::Options;
    using fable::launcher::ParseOptions;
    using fable::launcher::PrintUsage;

    bool IsArgument(const wchar_t* value, const wchar_t* expected)
    {
        return value != nullptr && _wcsicmp(value, expected) == 0;
    }

    int RunFirewallRepair(int argc, wchar_t** argv)
    {
        unsigned short port = 38171;
        for (int index = 2; index < argc; ++index)
        {
            if (!IsArgument(argv[index], L"--port") || ++index >= argc)
            {
                return 2;
            }
            wchar_t* end = nullptr;
            const unsigned long value = std::wcstoul(argv[index], &end, 10);
            if (end == argv[index] || *end != L'\0' ||
                value == 0 || value > 65'535)
            {
                return 2;
            }
            port = static_cast<unsigned short>(value);
        }
        std::wstring error;
        return fable::launcher::diagnostics::InstallFirewallRule(port, error)
            ? 0 : 1;
    }

}

int wmain(int argc, wchar_t** argv)
{
    // Establish DPI ownership before touching any console or window handle.
    // Calling this after Win32 has virtualized the process would scale the
    // native layout twice on high-DPI desktops.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc == 1 ||
        (argc == 2 && IsArgument(argv[1], L"--launcher-ui")))
    {
        return fable::launcher::ui::RunLauncherUi(GetModuleHandleW(nullptr));
    }
    if (argc >= 2 && IsArgument(argv[1], L"--repair-firewall"))
    {
        return RunFirewallRepair(argc, argv);
    }

    fable::launcher::platform::AttachCommandLineStreams();

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
    const fable::launcher::application::LaunchPlan plan = BuildLaunchPlan(
        options, launcherDirectory);
    PrintPlanSummary(plan);
    if (!ValidateLaunchPlan(plan))
    {
        return 1;
    }
    if (options.dryRun)
    {
        std::wcout << L"Dry run succeeded; no process was started.\n";
        return 0;
    }
    if (plan.mode == LaunchMode::DualInstance)
    {
        return RunDualInstanceTest(
            plan.executable,
            plan.clientDll,
            plan.artifactRoot,
            plan.runId,
            options.automationTimeoutSeconds,
            options.dualInstanceHoldSeconds,
            options.gameArguments);
    }
    if (plan.mode == LaunchMode::Multiplayer)
    {
        MultiplayerScenario scenario = MultiplayerScenario::Basic;
        if (options.multiplayerPlaytest && options.multiplayerCombatTest)
        {
            scenario = MultiplayerScenario::ManualCombat;
        }
        else if (options.multiplayerPlaytest && options.multiplayerRosterTest)
        {
            scenario = MultiplayerScenario::ManualRoster;
        }
        else if (options.multiplayerPlaytest)
        {
            scenario = MultiplayerScenario::Manual;
        }
        else if (options.multiplayerHeroWillTest)
        {
            scenario = MultiplayerScenario::HeroWill;
        }
        else if (options.multiplayerCombatTest)
        {
            scenario = MultiplayerScenario::Combat;
        }
        else if (options.multiplayerAuthorityTest)
        {
            scenario = MultiplayerScenario::Authority;
        }
        else if (options.multiplayerTransitionTest)
        {
            scenario = MultiplayerScenario::Transition;
        }
        else if (options.multiplayerRosterTest)
        {
            scenario = MultiplayerScenario::Roster;
        }
        MultiplayerTestContext context;
        context.executable = plan.executable;
        context.clientDll = plan.clientDll;
        context.fixtureDocumentsSource = plan.fixtureDocumentsSource;
        context.sessionRoot = plan.artifactRoot;
        context.sessionId = plan.runId;
        context.port = options.multiplayerPort;
        context.timeoutSeconds = options.automationTimeoutSeconds;
        context.scenario = scenario;
        context.gameArguments = options.gameArguments;
        return RunMultiplayerTest(context);
    }
    if (!PrepareLaunchArtifacts(plan))
    {
        return 1;
    }
    return RunSingleGame(plan);
}
