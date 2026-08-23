#include "GameWindowRuntime.h"
#include "Automation/TransformProbe/TransformProbeScenario.h"
#include "Core/Bootstrap/ClientRuntimeServices.h"
#include "Core/Bootstrap/FeatureRegistry.h"
#include "Automation/FrontEnd/FrontEndAutomationScenario.h"

namespace fable::ui::runtime
{
using namespace fable::core::bootstrap;
using namespace fable::automation::transform_probe;

    void OnGameWindowTimer()
    {
        PollHotkey();
    }

    void OnGameWindowFocusChanged(bool focused)
    {
        Log(focused
            ? "Event: Fable game window received keyboard focus."
            : "Event: Fable game window lost keyboard focus.");
    }

    void OnGameWindowDestroyed()
    {
        Log("Event: Fable game window is being destroyed.");
        LogEvent("ShutdownStarted", "game-window-destroyed");
    }

    bool OnGameWindowCloseRequested()
    {
        if (CoreContext().configuration.Scenario().empty())
        {
            return false;
        }
        Log("Automation: close requested; posting WM_QUIT to the game-window thread.");
        LogEvent("ShutdownStarted", "automation-window-close");
        PostQuitMessage(0);
        return true;
    }

    void OnGameWindowNumberRowOne(bool down, bool shiftPressed)
    {
        ObserveOneKeyState(
            down,
            shiftPressed,
            down ? "WM_KEYDOWN" : "WM_KEYUP");
    }

}

namespace
{
    using namespace fable::core::bootstrap;
    constexpr UINT_PTR kHotkeyTimerId = 0xFAB1;
    constexpr UINT kHotkeyPollIntervalMilliseconds = 16;

    bool GameWindowFeatureEnabled(const FeatureContext&) noexcept { return true; }

    bool InstallGameWindowFeature(FeatureContext& context) noexcept
    {
        if (IsPreResumeStage(context))
        {
            return true;
        }
        const fable::core::Diagnostics diagnostics = {Log, LogEvent};
        const fable::core::Diagnostics scriptDiagnostics = {ScriptLog, ScriptEvent};
        auto& ui = UiContext();
        auto& hooks = NativeHooksContext();
        const auto& configuration = CoreContext().configuration;
        const auto rollback = [&ui, &hooks]() noexcept
        {
            ui.mainWindow.Shutdown();
            ui.foregroundWindow.Shutdown();
            hooks.gameThreadIdle.Shutdown();
            ui.gameWindow = nullptr;
            ui.gameWindowThreadId = 0;
            return false;
        };

        ui.gameWindow = ui.mainWindow.WaitForWindow(
            diagnostics, CoreContext().cancelEvent);
        if (ui.gameWindow == nullptr)
        {
            return rollback();
        }
        ui.gameWindowThreadId = GetWindowThreadProcessId(ui.gameWindow, nullptr);
        LogEvent("GameWindowReady", "selected");
        if (configuration.Mode() == fable::automation::runtime::ClientMode::Observe &&
            !hooks.gameThreadIdle.Install(
                CoreContext().gameModule,
                ui.gameWindowThreadId,
                scriptDiagnostics,
                fable::automation::front_end::OnGameThreadIdle))
        {
            return rollback();
        }

        const bool preserveBackground =
            configuration.MultiplayerEnabled() && configuration.IsLocalInstance();
        if (preserveBackground &&
            !ui.foregroundWindow.Install(CoreContext().gameModule, ui.gameWindow, diagnostics))
        {
            return rollback();
        }
        const fable::ui::MainWindowCallbacks callbacks = {
            fable::ui::runtime::OnGameWindowTimer,
            fable::ui::runtime::OnGameWindowFocusChanged,
            fable::ui::runtime::OnGameWindowDestroyed,
            fable::ui::runtime::OnGameWindowCloseRequested,
            fable::ui::runtime::OnGameWindowNumberRowOne,
        };
        const bool captureOne =
            configuration.Mode() == fable::automation::runtime::ClientMode::TransformProbe ||
            configuration.Mode() == fable::automation::runtime::ClientMode::AppearanceCycle;
        const bool installed = ui.mainWindow.Install(
            ui.gameWindow,
            kHotkeyTimerId,
            kHotkeyPollIntervalMilliseconds,
            captureOne,
            preserveBackground,
            callbacks,
            diagnostics);
        if (!installed)
        {
            return rollback();
        }
        if (preserveBackground)
        {
            LogEvent(
                "MultiplayerBackgroundRenderingEnabled",
                "local peer keeps retail actor and animation simulation active while retaining normal Windows and DirectInput focus");
        }
        return true;
    }

    void UninstallGameWindowFeature(FeatureContext& context) noexcept
    {
        if (IsPreResumeStage(context))
        {
            return;
        }
        UiContext().mainWindow.Shutdown();
        UiContext().foregroundWindow.Shutdown();
        NativeHooksContext().gameThreadIdle.Shutdown();
        UiContext().gameWindow = nullptr;
        UiContext().gameWindowThreadId = 0;
    }

    FABLE_FEATURE_DEPENDENCIES(
        gameWindowDependencies,
        "automation.frontend",
        "automation.character-snapshot");
    FABLE_FEATURE_DESCRIPTOR(
        fableGameWindowFeature,
        "ui.game-window",
        "Game window runtime",
        FeaturePhase::GameThread,
        0,
        GameWindowFeatureEnabled,
        gameWindowDependencies,
        std::size(gameWindowDependencies),
        InstallGameWindowFeature,
        UninstallGameWindowFeature,
        "game-window-runtime-installation");
}
