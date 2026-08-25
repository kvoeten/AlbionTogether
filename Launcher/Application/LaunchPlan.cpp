#include "LaunchPlan.h"

#include "../Artifacts/RunId.h"
#include "../Configuration/LauncherConstants.h"
#include "../Configuration/Paths.h"
#include "../Automation/WindowControl.h"

#include <Windows.h>

#include <algorithm>
#include <iostream>

namespace fable::launcher::application
{
    namespace
    {
        bool IsMultiplayer(const Options& options)
        {
            return options.multiplayerTest || options.multiplayerRosterTest ||
                options.multiplayerTransitionTest || options.multiplayerAuthorityTest ||
                options.multiplayerCombatTest || options.multiplayerHeroWillTest ||
                options.multiplayerPlaytest;
        }

        bool UsesHero3Fixture(const Options& options)
        {
            return options.multiplayerCombatTest || options.multiplayerHeroWillTest ||
                (options.multiplayerRosterTest && options.multiplayerPlaytest);
        }

        bool IsLocalInstance(const Options& options)
        {
            return !options.localInstance.empty();
        }

        LaunchMode SelectMode(const Options& options)
        {
            if (options.dualInstanceTest)
            {
                return LaunchMode::DualInstance;
            }
            if (IsMultiplayer(options))
            {
                return LaunchMode::Multiplayer;
            }
            if (options.transformationProbe)
            {
                return LaunchMode::TransformProbe;
            }
            if (IsLocalInstance(options))
            {
                return LaunchMode::LocalInstance;
            }
            if (!options.automationScenario.empty())
            {
                return LaunchMode::Automation;
            }
            return LaunchMode::Normal;
        }

        bool IsFixtureMode(const Options& options)
        {
            return options.automationScenario == L"load_fixture" ||
                options.automationScenario == L"appearance_cycle" ||
                IsMultiplayer(options);
        }

        const wchar_t* MultiplayerTestName(const Options& options)
        {
            if (options.multiplayerPlaytest)
            {
                if (options.multiplayerCombatTest)
                {
                    return L"multiplayer_combat_manual";
                }
                if (options.multiplayerRosterTest)
                {
                    return L"multiplayer_chamber_roster_manual";
                }
                return L"multiplayer_adult_town_manual";
            }
            if (options.multiplayerHeroWillTest)
            {
                return L"multiplayer_hero_will";
            }
            if (options.multiplayerCombatTest)
            {
                return L"multiplayer_combat_authority_handoff";
            }
            if (options.multiplayerAuthorityTest)
            {
                return L"multiplayer_map_authority_handoff";
            }
            if (options.multiplayerTransitionTest)
            {
                return L"multiplayer_map_transition";
            }
            if (options.multiplayerRosterTest)
            {
                return L"multiplayer_three_peer_roster";
            }
            return L"multiplayer_adult_town";
        }

        std::filesystem::path ResolveFixtureSource(
            const Options& options,
            const std::filesystem::path& launcherDirectory,
            bool loadFixtureScenario)
        {
            if (!loadFixtureScenario)
            {
                return {};
            }
            if (!options.fixtureDocuments.empty())
            {
                return fable::launcher::AbsolutePath(options.fixtureDocuments);
            }
            const std::filesystem::path fixtureName = UsesHero3Fixture(options)
                ? L"combat-chamber-hero3"
                : L"adult-town";
            return fable::launcher::ResolveDeploymentAsset(
                launcherDirectory,
                std::filesystem::path(L"fixtures") / fixtureName / L"Documents",
                true);
        }
    }

    LaunchPlan BuildLaunchPlan(
        const Options& options,
        const std::filesystem::path& launcherDirectory)
    {
        LaunchPlan plan;
        plan.options = options;
        plan.launcherDirectory = launcherDirectory;
        plan.mode = SelectMode(options);
        plan.executable = fable::launcher::ResolveExecutable(
            options, launcherDirectory);
        plan.clientDll = options.clientDll.empty()
            ? fable::launcher::AbsolutePath(
                launcherDirectory / fable::launcher::kClientDllName)
            : fable::launcher::AbsolutePath(options.clientDll);
        plan.runId = fable::launcher::CreateRunId();
        const bool local = IsLocalInstance(options);
        plan.localSession = local
            ? options.localSession.empty() ? plan.runId : options.localSession
            : std::wstring();
        const std::wstring artifactId = local ? plan.localSession : plan.runId;
        plan.artifactRoot = fable::launcher::AbsolutePath(
            plan.clientDll.parent_path() / L"artifacts" / artifactId);
        plan.instanceRoot = local
            ? plan.artifactRoot / options.localInstance
            : plan.artifactRoot;
        plan.eventPath = plan.instanceRoot / L"events.jsonl";
        plan.clientLog = plan.instanceRoot / L"client.log";
        plan.characterSnapshotSource = options.characterSnapshot.empty()
            ? std::filesystem::path()
            : fable::launcher::AbsolutePath(options.characterSnapshot);
        plan.characterSnapshot = plan.characterSnapshotSource.empty()
            ? std::filesystem::path()
            : plan.eventPath.parent_path() / L"character-snapshot.json";
        plan.loadFixtureScenario = IsFixtureMode(options);
        plan.fixtureDocumentsSource = ResolveFixtureSource(
            options, launcherDirectory, plan.loadFixtureScenario);
        const std::filesystem::path defaultFixtureDocuments =
            options.automationScenario == L"bootstrap_fixture_probe"
            ? plan.clientDll.parent_path() / L"fixtures" / L"bootstrap" /
                plan.runId / L"Documents"
            : plan.loadFixtureScenario
            ? plan.clientDll.parent_path() / L"fixtures" / L"load" /
                plan.runId / L"Documents"
            : plan.clientDll.parent_path() / L"fixtures" / L"automation" /
                L"Documents";
        plan.fixtureDocuments = local
            ? plan.instanceRoot / L"Documents"
            : options.automationScenario.empty()
            ? std::filesystem::path()
            : fable::launcher::AbsolutePath(
                options.fixtureDocuments.empty() || plan.loadFixtureScenario
                    ? defaultFixtureDocuments
                    : options.fixtureDocuments);
        plan.scriptData = local
            ? plan.instanceRoot / L"script-data"
            : std::filesystem::path();
        plan.clientMode = options.transformationProbe
            ? L"transform_probe"
            : !options.multiplayerRole.empty()
            ? L"observe"
            : local || options.dualInstanceTest
            ? L"observe"
            : options.automationScenario.empty()
            ? L"appearance_cycle"
            : L"observe";
        return plan;
    }

    std::vector<std::wstring> BuildGameArguments(const LaunchPlan& plan)
    {
        std::vector<std::wstring> arguments = plan.options.gameArguments;
        if (!plan.options.automationScenario.empty())
        {
            const bool alreadySkipsMovies = std::any_of(
                arguments.begin(),
                arguments.end(),
                [](const std::wstring& argument)
                {
                    return _wcsicmp(argument.c_str(), L"-nomoviestartup") == 0;
                });
            if (!alreadySkipsMovies)
            {
                arguments.emplace_back(L"-nomoviestartup");
            }
        }
        if (IsLocalInstance(plan.options))
        {
            arguments = automation::LocalWindowArguments(arguments);
        }
        return arguments;
    }

    void PrintPlanSummary(const LaunchPlan& plan)
    {
        std::wcout << L"Game:   "
                   << (plan.executable.empty()
                       ? L"<not found>" : plan.executable.wstring()) << L'\n'
                   << L"Client: " << plan.clientDll.wstring() << L'\n'
                   << L"Mode:   " << plan.clientMode << L'\n'
                   << L"Run:    " << plan.runId << L'\n';
        const bool multiplayer = IsMultiplayer(plan.options);
        if (!plan.options.dualInstanceTest && !multiplayer)
        {
            std::wcout << L"Log:    " << plan.clientLog.wstring() << L'\n'
                       << L"Events: " << plan.eventPath.wstring() << L'\n';
        }
        if (IsLocalInstance(plan.options))
        {
            std::wcout << L"Local:  session=" << plan.localSession
                       << L" instance=" << plan.options.localInstance << L'\n'
                       << L"Documents: " << plan.fixtureDocuments.wstring() << L'\n'
                       << L"Script data: " << plan.scriptData.wstring() << L'\n';
        }
        if (!plan.options.multiplayerRole.empty())
        {
            std::wcout << L"Multiplayer: role=" << plan.options.multiplayerRole
                       << L" port=" << plan.options.multiplayerPort
                       << L" player=" << plan.options.multiplayerPlayerId
                       << L" appearance=" << plan.options.multiplayerAppearance << L'\n';
            if (plan.options.multiplayerRole == L"guest")
            {
                std::wcout << L"Host:   " << plan.options.multiplayerAddress << L'\n';
            }
        }
        if (plan.options.dualInstanceTest)
        {
            std::wcout << L"Test:   dual_instance_title_screen\n"
                       << L"State:  " << plan.artifactRoot.wstring() << L'\n';
        }
        if (multiplayer)
        {
            std::wcout << L"Test:   " << MultiplayerTestName(plan.options) << L"\n"
                       << L"Fixture Source: " << plan.fixtureDocumentsSource.wstring() << L'\n'
                       << L"State:  " << plan.artifactRoot.wstring() << L'\n';
        }
        if (!plan.options.automationScenario.empty())
        {
            std::wcout << L"Test:   " << plan.options.automationScenario << L'\n';
            if (plan.loadFixtureScenario)
            {
                std::wcout << L"Fixture Source: "
                           << plan.fixtureDocumentsSource.wstring() << L'\n';
            }
            std::wcout << L"Fixture Documents: "
                       << plan.fixtureDocuments.wstring() << L'\n';
            if (!plan.characterSnapshotSource.empty())
            {
                std::wcout << L"Character Snapshot Source: "
                           << plan.characterSnapshotSource.wstring() << L'\n'
                           << L"Character Snapshot: "
                           << plan.characterSnapshot.wstring() << L'\n';
            }
        }
    }

    bool ValidateLaunchPlan(const LaunchPlan& plan)
    {
        if (plan.executable.empty() || !fable::launcher::IsFile(plan.executable))
        {
            std::wcerr << L"Fable Anniversary.exe was not found. Use --game-dir or --exe.\n";
            return false;
        }
        if (!fable::launcher::IsFile(plan.clientDll))
        {
            std::wcerr << L"FableTogether.Client.dll was not found beside the launcher. Use --dll to override.\n";
            return false;
        }
        if (!plan.characterSnapshotSource.empty() &&
            !fable::launcher::IsFile(plan.characterSnapshotSource))
        {
            std::wcerr << L"The character snapshot is not an existing file.\n";
            return false;
        }
        if (plan.loadFixtureScenario)
        {
            const std::filesystem::path ordinaryDocuments =
                fable::launcher::GetOrdinaryDocumentsPath();
            if (!fable::launcher::IsDirectory(plan.fixtureDocumentsSource))
            {
                std::wcerr << L"The load fixture source is not an existing directory.\n";
                return false;
            }
            if (!ordinaryDocuments.empty() &&
                fable::launcher::IsSamePathOrBelow(
                    plan.fixtureDocumentsSource, ordinaryDocuments))
            {
                std::wcerr << L"Refusing to load a fixture from the ordinary Documents tree.\n";
                return false;
            }
            const std::filesystem::path saveRoot = plan.fixtureDocumentsSource /
                L"My Games" / L"FableHD" / L"Saves" /
                (UsesHero3Fixture(plan.options) ? L"Hero3" : L"Hero1");
            if (!fable::launcher::IsFile(saveRoot / L"Profile.bin") ||
                !fable::launcher::IsFile(saveRoot / L"AutoSave"))
            {
                std::wcerr << L"The fixture must contain an isolated Profile.bin and AutoSave pair.\n";
                return false;
            }
        }
        return true;
    }

    bool PrepareLaunchArtifacts(const LaunchPlan& plan)
    {
        std::error_code artifactError;
        std::filesystem::create_directories(
            plan.eventPath.parent_path(), artifactError);
        if (artifactError)
        {
            std::wcerr << L"Could not create the run artifact directory: "
                       << artifactError.message().c_str() << L'\n';
            return false;
        }
        if (!plan.characterSnapshotSource.empty())
        {
            std::filesystem::copy_file(
                plan.characterSnapshotSource,
                plan.characterSnapshot,
                std::filesystem::copy_options::overwrite_existing,
                artifactError);
            if (artifactError)
            {
                std::wcerr << L"Could not copy the character snapshot into the immutable run artifacts: "
                           << artifactError.message().c_str() << L'\n';
                return false;
            }
        }
        if (!plan.fixtureDocuments.empty())
        {
            std::error_code fixtureError;
            std::filesystem::create_directories(
                plan.fixtureDocuments, fixtureError);
            if (fixtureError)
            {
                std::wcerr << L"Could not create the isolated fixture Documents directory: "
                           << fixtureError.message().c_str() << L'\n';
                return false;
            }
            if (plan.loadFixtureScenario)
            {
                std::filesystem::copy(
                    plan.fixtureDocumentsSource,
                    plan.fixtureDocuments,
                    std::filesystem::copy_options::recursive |
                        std::filesystem::copy_options::overwrite_existing,
                    fixtureError);
                if (fixtureError)
                {
                    std::wcerr << L"Could not copy the isolated fixture into its run-specific working directory: "
                               << fixtureError.message().c_str() << L'\n';
                    return false;
                }
            }
        }
        if (!plan.scriptData.empty())
        {
            std::error_code storageError;
            std::filesystem::create_directories(plan.scriptData, storageError);
            if (storageError)
            {
                std::wcerr << L"Could not create the isolated script-data directory: "
                           << storageError.message().c_str() << L'\n';
                return false;
            }
        }
        return true;
    }
}
