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
                options.multiplayerTransitionTest || options.multiplayerMapStressTest ||
                options.multiplayerSaveTest ||
                options.multiplayerAuthorityTest ||
                options.multiplayerCombatTest || options.multiplayerHeroWillTest ||
                options.multiplayerPlaytest;
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
            if (options.multiplayerMapStressTest)
            {
                return L"multiplayer_map_stress";
            }
            if (options.multiplayerSaveTest)
            {
                return L"multiplayer_save_reload";
            }
            if (options.multiplayerRosterTest)
            {
                return L"multiplayer_three_peer_roster";
            }
            return L"multiplayer_adult_town";
        }

        std::filesystem::path ResolveFixtureSource(
            const Options& options,
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
            // Test characters are deliberately never selected implicitly.
            // Every fixture-backed run must name its isolated save source so
            // distinct real characters cannot regress to bundled lookalikes.
            return {};
        }

        bool ContainsOneCompleteHeroFixture(
            const std::filesystem::path& documents)
        {
            const std::filesystem::path saves = documents / L"My Games" /
                L"FableHD" / L"Saves";
            std::error_code error;
            unsigned int completeHeroes = 0;
            for (std::filesystem::directory_iterator iterator(saves, error), end;
                 !error && iterator != end;
                 iterator.increment(error))
            {
                if (!iterator->is_directory(error))
                {
                    continue;
                }
                if (std::filesystem::is_regular_file(
                        iterator->path() / L"Profile.bin", error) &&
                    std::filesystem::is_regular_file(
                        iterator->path() / L"AutoSave", error))
                {
                    ++completeHeroes;
                }
            }
            return !error && completeHeroes == 1;
        }

        bool ContainsCompletePairedHeroFixture(
            const std::filesystem::path& fixtureRoot)
        {
            return ContainsOneCompleteHeroFixture(
                       fixtureRoot / L"host" / L"Documents") &&
                ContainsOneCompleteHeroFixture(
                       fixtureRoot / L"guest" / L"Documents");
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
            options, plan.loadFixtureScenario);
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
            ? options.fixtureDocuments.empty()
                ? std::filesystem::path()
                : fable::launcher::AbsolutePath(options.fixtureDocuments)
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
                       << L"Documents: "
                       << (plan.fixtureDocuments.empty()
                           ? L"native Fable save directory"
                           : plan.fixtureDocuments.wstring()) << L'\n'
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
            std::wcerr << L"AlbionTogether.Client.dll was not found beside the launcher. Use --dll to override.\n";
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
                std::wcerr << L"Fixture-backed tests require --fixture-documents pointing to either one isolated Documents directory or a paired root containing host/Documents and guest/Documents.\n";
                return false;
            }
            if (!ordinaryDocuments.empty() &&
                fable::launcher::IsSamePathOrBelow(
                    plan.fixtureDocumentsSource, ordinaryDocuments))
            {
                std::wcerr << L"Refusing to load a fixture from the ordinary Documents tree.\n";
                return false;
            }
            if (!ContainsOneCompleteHeroFixture(
                    plan.fixtureDocumentsSource) &&
                !ContainsCompletePairedHeroFixture(
                    plan.fixtureDocumentsSource))
            {
                std::wcerr << L"The fixture must contain exactly one isolated Hero directory, or distinct host/Documents and guest/Documents fixtures, each with one Profile.bin and AutoSave pair.\n";
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
