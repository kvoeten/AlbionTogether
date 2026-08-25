#include "AutomationRunner.h"

#include "InputStimulus.h"
#include "WindowControl.h"
#include "../Diagnostics/EventLog.h"

#include <Windows.h>

#include <iostream>

namespace fable::launcher::automation
{
    namespace
    {
        enum class ScenarioProgress
        {
            Pending,
            Passed,
            Failed
        };

        ScenarioProgress FinishScenario(
            runtime::LaunchedGame& game,
            const std::filesystem::path& eventPath,
            const wchar_t* passedMessage,
            const wchar_t* failureMessage)
        {
            const bool graceful = runtime::CloseCreatedProcess(
                game.process.get(),
                game.processId,
                game.shutdownEvent.get());
            const std::string finalEvents =
                fable::launcher::diagnostics::ReadEventFile(eventPath);
            if (!graceful ||
                !fable::launcher::diagnostics::EventWasReported(
                    finalEvents, "ShutdownStarted"))
            {
                std::wcerr << failureMessage << L"\n";
                return ScenarioProgress::Failed;
            }
            std::wcout << passedMessage << L"\n";
            return ScenarioProgress::Passed;
        }

        ScenarioProgress ObserveFrontend(
            runtime::LaunchedGame& game,
            const std::filesystem::path& eventPath,
            const std::string& events)
        {
            if (!fable::launcher::diagnostics::EventWasReported(
                    events, "FrontendReady"))
            {
                return ScenarioProgress::Pending;
            }
            std::wcout << L"Automation: Fable front-end main menu reached.\n";
            return FinishScenario(
                game,
                eventPath,
                L"Automation passed: front end reached and process shut down cleanly.",
                L"Automation failed: front end was reached, but shutdown was not cleanly observed.");
        }

        ScenarioProgress ObserveSaveList(
            runtime::LaunchedGame& game,
            const std::filesystem::path& eventPath,
            const std::string& events)
        {
            if (!fable::launcher::diagnostics::EventWasReported(
                    events, "SaveListReady"))
            {
                return ScenarioProgress::Pending;
            }
            std::wcout << L"Automation: Fable Load Game save list reached without selecting a save.\n";
            return FinishScenario(
                game,
                eventPath,
                L"Automation passed: save list observed and process shut down.",
                L"Automation failed: save list was reached, but shutdown was not observed.");
        }

        ScenarioProgress ObserveBootstrap(
            runtime::LaunchedGame& game,
            const std::filesystem::path& eventPath,
            const std::string& events)
        {
            if (!fable::launcher::diagnostics::EventWasReported(
                    events, "HeroReady"))
            {
                return ScenarioProgress::Pending;
            }
            std::wcout << L"Automation: isolated New Game reached a resolvable Hero in the playable world.\n";
            return FinishScenario(
                game,
                eventPath,
                L"Automation passed: isolated New Game Hero readiness observed.",
                L"Automation failed: Hero became ready, but shutdown was not observed.");
        }

        ScenarioProgress ObserveFixture(
            runtime::LaunchedGame& game,
            const std::filesystem::path& eventPath,
            const std::string& events,
            bool characterSnapshotExpected)
        {
            const bool passed = characterSnapshotExpected
                ? fable::launcher::diagnostics::EventWasReported(
                    events, "CharacterSnapshotAssertionPassed")
                : fable::launcher::diagnostics::EventWasReported(
                    events, "AssertionPassed");
            if (!passed)
            {
                return ScenarioProgress::Pending;
            }
            std::wcout << (characterSnapshotExpected
                ? L"Automation: server-character snapshot produced stable target transform and combat health.\n"
                : L"Automation: exact isolated AutoSave produced stable Hero transform and active-creature state.\n");
            const ScenarioProgress progress = FinishScenario(
                game,
                eventPath,
                characterSnapshotExpected
                    ? L"Automation passed: exact isolated AutoSave loaded, the server-character snapshot was applied and verified, and the process shut down."
                    : L"Automation passed: exact isolated AutoSave loaded, Hero state was verified, and the process shut down.",
                L"Automation failed: loaded fixture assertions passed, but shutdown was not observed.");
            return progress;
        }

        ScenarioProgress ObserveAppearance(
            runtime::LaunchedGame& game,
            const std::filesystem::path& eventPath,
            const std::string& events)
        {
            if (!fable::launcher::diagnostics::EventWasReported(
                    events, "AppearanceCyclePassed"))
            {
                return ScenarioProgress::Pending;
            }
            std::wcout << L"Automation: AngelScript created guard, villager, and hobbe forms; verified Hero frame displacement produced native guard navigator requests, physical displacement, locomotion input, and animation-state activity while the authoritative Hero remained stable.\n";
            const bool graceful = runtime::CloseCreatedProcess(
                game.process.get(),
                game.processId,
                game.shutdownEvent.get());
            const std::string finalEvents =
                fable::launcher::diagnostics::ReadEventFile(eventPath);
            if (!graceful ||
                !fable::launcher::diagnostics::EventWasReported(
                    finalEvents, "ShutdownStarted") ||
                !fable::launcher::diagnostics::EventWasReported(
                    finalEvents, "PlayerAttackAbilityHookReady") ||
                !fable::launcher::diagnostics::EventWasReported(
                    finalEvents, "PlayerAttackAbilityIntercepted"))
            {
                std::wcerr << L"Automation failed: appearance assertions passed, but deep native player ATTACK ability interception or clean shutdown was not observed.\n";
                return ScenarioProgress::Failed;
            }
            std::wcout << L"Automation passed: native locomotion, player-owned facing, hidden-Hero shadow follow, friendly decision ownership, native player ATTACK-to-NPC ability routing, three-form cycling, Hero restoration, and clean shutdown were all observed.\n";
            return ScenarioProgress::Passed;
        }

        struct AppearanceInputState
        {
            ScopedSyntheticKey movementKey;
            ScopedSyntheticMouseButton attackButton;
            bool movementInputSubmitted = false;
            ULONGLONG movementInputPressedAt = 0;
            unsigned int attackInputAttempts = 0;
            ULONGLONG attackInputPressedAt = 0;
            ULONGLONG attackInputReleasedAt = 0;

            AppearanceInputState()
                : movementKey('W')
            {
            }
        };

        bool DriveAppearanceInput(
            runtime::LaunchedGame& game,
            const std::string& events,
            AppearanceInputState& state)
        {
            if (!state.movementInputSubmitted &&
                fable::launcher::diagnostics::EventWasReported(
                    events, "AppearanceFormReady"))
            {
                const HWND window = FindMainWindow(game.processId);
                if (window != nullptr)
                {
                    SetForegroundWindow(window);
                }
                if (!state.movementKey.Press())
                {
                    std::wcerr << L"Automation failed: could not press W for the native NPC movement probe.\n";
                    return false;
                }
                state.movementInputSubmitted = true;
                state.movementInputPressedAt = GetTickCount64();
                std::wcout << L"Automation: holding W briefly to test native NPC locomotion ownership.\n";
            }
            if (state.movementKey.down() &&
                GetTickCount64() - state.movementInputPressedAt >= 750)
            {
                if (!state.movementKey.Release())
                {
                    std::wcerr << L"Automation failed: could not release W after the native NPC movement probe.\n";
                    return false;
                }
                std::wcout << L"Automation: released W after the native NPC movement probe.\n";
            }
            if (state.movementInputSubmitted && !state.movementKey.down() &&
                !state.attackButton.down() &&
                !fable::launcher::diagnostics::EventWasReported(
                    events, "PlayerAttackAbilityIntercepted") &&
                state.attackInputAttempts < 5 &&
                (state.attackInputAttempts == 0 ||
                    GetTickCount64() - state.attackInputReleasedAt >= 250) &&
                fable::launcher::diagnostics::EventWasReported(
                    events, "CreaturePlayerCombatRouterBound"))
            {
                const HWND window = FindMainWindow(game.processId);
                if (window != nullptr)
                {
                    SetForegroundWindow(window);
                }
                if (!state.attackButton.Press(window))
                {
                    std::wcerr << L"Automation failed: could not submit the mapped ATTACK stimulus.\n";
                    return false;
                }
                ++state.attackInputAttempts;
                state.attackInputPressedAt = GetTickCount64();
                std::wcout << L"Automation: submitted mapped game-window ATTACK stimulus "
                           << state.attackInputAttempts << L"/5 for the native combat boundary.\n";
            }
            if (state.attackButton.down() &&
                GetTickCount64() - state.attackInputPressedAt >= 100)
            {
                if (!state.attackButton.Release())
                {
                    std::wcerr << L"Automation failed: could not release the mapped ATTACK stimulus.\n";
                    return false;
                }
                state.attackInputReleasedAt = GetTickCount64();
            }
            return true;
        }
    }

    int RunAutomation(
        runtime::LaunchedGame& game,
        const std::filesystem::path& eventPath,
        const std::wstring& scenario,
        unsigned int timeoutSeconds,
        bool characterSnapshotExpected)
    {
        std::wcout << L"Automation: waiting up to " << timeoutSeconds
                   << L" seconds for scenario " << scenario << L".\n";
        const ULONGLONG deadline = GetTickCount64() +
            static_cast<ULONGLONG>(timeoutSeconds) * 1'000;
        AppearanceInputState inputState;
        for (;;)
        {
            const std::string events =
                fable::launcher::diagnostics::ReadEventFile(eventPath);
            if (scenario == L"appearance_cycle" &&
                !DriveAppearanceInput(game, events, inputState))
            {
                runtime::CloseCreatedProcess(
                    game.process.get(), game.processId, game.shutdownEvent.get());
                return 1;
            }
            if (fable::launcher::diagnostics::EventWasReported(
                    events, "ClientFailed"))
            {
                std::wcerr << L"Automation failed: the injected client reported a hook failure.\n";
                runtime::CloseCreatedProcess(
                    game.process.get(), game.processId, game.shutdownEvent.get());
                return 1;
            }
            ScenarioProgress progress = ScenarioProgress::Pending;
            if (scenario == L"observe_frontend")
            {
                progress = ObserveFrontend(game, eventPath, events);
            }
            else if (scenario == L"observe_save_list")
            {
                progress = ObserveSaveList(game, eventPath, events);
            }
            else if (scenario == L"bootstrap_fixture_probe")
            {
                progress = ObserveBootstrap(game, eventPath, events);
            }
            else if (scenario == L"load_fixture")
            {
                progress = ObserveFixture(
                    game, eventPath, events, characterSnapshotExpected);
            }
            else if (scenario == L"appearance_cycle")
            {
                progress = ObserveAppearance(game, eventPath, events);
            }
            if (progress != ScenarioProgress::Pending)
            {
                return progress == ScenarioProgress::Passed ? 0 : 1;
            }
            const DWORD processState = WaitForSingleObject(
                game.process.get(), 250);
            if (processState == WAIT_OBJECT_0)
            {
                DWORD exitCode = 0;
                GetExitCodeProcess(game.process.get(), &exitCode);
                std::wcerr << L"Automation failed: Fable exited before the scenario completed; exit code "
                           << exitCode << L".\n";
                return 1;
            }
            if (processState == WAIT_FAILED)
            {
                std::wcerr << L"Automation failed while monitoring the Fable process.\n";
                runtime::CloseCreatedProcess(
                    game.process.get(), game.processId, game.shutdownEvent.get());
                return 1;
            }
            if (GetTickCount64() >= deadline)
            {
                std::wcerr << L"Automation failed: scenario timed out.\n";
                runtime::CloseCreatedProcess(
                    game.process.get(), game.processId, game.shutdownEvent.get());
                return 1;
            }
        }
    }
}
