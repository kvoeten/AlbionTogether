#pragma once

#include "ClientRuntimeState.h"

#include <string>

namespace fable::core::bootstrap
{
    CoreRuntimeContext& CoreContext() noexcept;
    DiagnosticsRuntimeContext& DiagnosticsContext() noexcept;
    NativeHooksRuntimeContext& NativeHooksContext() noexcept;
    GameplayRuntimeContext& GameplayContext() noexcept;
    AutomationRuntimeContext& AutomationContext() noexcept;
    CharacterSnapshotRuntimeContext& CharacterSnapshotContext() noexcept;
    FrontEndAutomationRuntimeContext& FrontEndContext() noexcept;
    TransformProbeRuntimeContext& TransformContext() noexcept;
    UiRuntimeContext& UiContext() noexcept;

    [[nodiscard]] FeatureLifecycleContext& FeatureLifecycle(
        FeatureContext& context) noexcept;
    [[nodiscard]] const FeatureLifecycleContext& FeatureLifecycle(
        const FeatureContext& context) noexcept;
    [[nodiscard]] bool IsPreResumeStage(const FeatureContext& context) noexcept;

    std::string WideToUtf8(const wchar_t* value);
    void Log(const char* message);
    void LogFormat(const char* format, ...);
    void LogEvent(const char* state, const char* detail = "");
    void ScriptLog(const char* message);
    void ScriptEvent(const char* state, const char* detail);
    void LogStartupContext();
    bool ValidateExecutable();

    bool ScenarioIs(const wchar_t* value);
    bool ScenarioUsesFrontEndStartAutomation();
    bool ScenarioLoadsFixture();

    bool ResolveGameInterface(GameScriptInterface*& gameInterface);
    bool RetainTransformHandle(ScriptThing& thing, const char* label);
}
