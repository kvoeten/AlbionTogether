#include "Configuration/LauncherConstants.h"
#include "Configuration/Options.h"
#include "Configuration/Paths.h"
#include "Automation/DualInstanceScenario.h"
#include "Application/LaunchPlan.h"
#include "Application/SingleGameLaunch.h"
#include "Multiplayer/Scenarios/ScenarioRunners.h"

#include <filesystem>
#include <iostream>
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
