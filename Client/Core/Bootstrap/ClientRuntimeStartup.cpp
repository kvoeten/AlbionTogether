#include "Core/Bootstrap/ClientRuntimeStartup.h"

#include "Core/Bootstrap/ClientRuntime.h"
#include "Core/Bootstrap/ClientRuntimeServices.h"
#include "Core/Bootstrap/FeatureRegistry.h"

#include <array>

namespace fable::core::bootstrap
{
    namespace
    {
        constexpr wchar_t kClientVersion[] = L"development-current";

        void CompleteBootstrap(
            CoreRuntimeContext& core,
            const ClientRuntimeStatus status,
            const ClientInitializationResult result) noexcept
        {
            core.failureCode.store(
                status == ClientRuntimeStatus::Ready
                    ? ERROR_SUCCESS
                    : static_cast<DWORD>(result),
                std::memory_order_release);
            core.status.store(status, std::memory_order_release);
            SetEvent(core.completionEvent);
        }
    }

    void LogStartupContext()
    {
        wchar_t currentDirectory[32'768] = {};
        wchar_t steamAppId[64] = {};
        GetCurrentDirectoryW(static_cast<DWORD>(std::size(currentDirectory)), currentDirectory);
        GetEnvironmentVariableW(L"SteamAppId", steamAppId, static_cast<DWORD>(std::size(steamAppId)));
        const auto& configuration = CoreContext().configuration;

        LogFormat(
            "Startup: build=%ls pid=%lu bootstrap_tid=%lu game_base=%p client_base=%p.",
            kClientVersion,
            static_cast<unsigned long>(GetCurrentProcessId()),
            static_cast<unsigned long>(GetCurrentThreadId()),
            CoreContext().gameModule,
            CoreContext().clientModule);
        LogFormat("Startup: current_directory=%s.", WideToUtf8(currentDirectory).c_str());
        LogFormat("Startup: command_line=%s.", WideToUtf8(GetCommandLineW()).c_str());
        LogFormat(
            "Startup: SteamAppId=%s steam_api=%s.",
            WideToUtf8(steamAppId).empty() ? "<unset>" : WideToUtf8(steamAppId).c_str(),
            GetModuleHandleW(L"steam_api.dll") == nullptr ? "not-loaded" : "loaded");
        LogFormat(
            "Startup: mode=%s run_id=%s scenario=%s events=%s.",
            configuration.Mode() == fable::automation::runtime::ClientMode::TransformProbe
                ? "transform_probe"
                : "observe",
            configuration.RunId().empty() ? "<unset>" : WideToUtf8(configuration.RunId().c_str()).c_str(),
            configuration.Scenario().empty() ? "<none>" : WideToUtf8(configuration.Scenario().c_str()).c_str(),
            configuration.EventPath().empty() ? "<unset>" : WideToUtf8(configuration.EventPath().c_str()).c_str());
    }

    DWORD WINAPI BootstrapThread(void* parameter)
    {
        auto* runtime = static_cast<ClientRuntimeContext*>(parameter);
        if (runtime == nullptr)
        {
            return static_cast<DWORD>(ClientInitializationResult::InvalidState);
        }
        HANDLE gates[] = {runtime->core.cancelEvent, runtime->core.resumeEvent};
        const DWORD gate = WaitForMultipleObjects(
            static_cast<DWORD>(std::size(gates)), gates, FALSE, INFINITE);
        if (gate == WAIT_OBJECT_0)
        {
            CompleteBootstrap(
                runtime->core,
                ClientRuntimeStatus::Failed,
                ClientInitializationResult::RuntimeCancelled);
            return static_cast<DWORD>(ClientInitializationResult::RuntimeCancelled);
        }
        if (gate != WAIT_OBJECT_0 + 1)
        {
            CompleteBootstrap(
                runtime->core,
                ClientRuntimeStatus::Failed,
                ClientInitializationResult::RuntimeFailed);
            return static_cast<DWORD>(ClientInitializationResult::RuntimeFailed);
        }

        runtime->core.status.store(ClientRuntimeStatus::Starting, std::memory_order_release);
        FeaturePlan plan{};
        if (!BuildLinkerFeaturePlan(runtime->postResumeFeatureContext, plan))
        {
            LogEvent("ClientFailed", "runtime-feature-plan");
            CompleteBootstrap(
                runtime->core,
                ClientRuntimeStatus::Failed,
                ClientInitializationResult::FeaturePlanFailed);
            return static_cast<DWORD>(ClientInitializationResult::FeaturePlanFailed);
        }

        FeatureInstallFailure failure{};
        if (!runtime->postResumeFeatures.Install(
                plan, runtime->postResumeFeatureContext, failure))
        {
            LogFormat(
                "Startup: runtime feature failed; feature=%s identity=%s.",
                failure.featureId != nullptr ? failure.featureId : "<unknown>",
                failure.failureIdentity != nullptr ? failure.failureIdentity : "<unknown>");
            LogEvent(
                "ClientFailed",
                failure.failureIdentity != nullptr
                    ? failure.failureIdentity
                    : "runtime-feature-installation");
            CompleteBootstrap(
                runtime->core,
                ClientRuntimeStatus::Failed,
                ClientInitializationResult::RuntimeFeatureFailed);
            return static_cast<DWORD>(ClientInitializationResult::RuntimeFeatureFailed);
        }

        if (WaitForSingleObject(runtime->core.cancelEvent, 0) == WAIT_OBJECT_0)
        {
            runtime->postResumeFeatures.Shutdown();
            CompleteBootstrap(
                runtime->core,
                ClientRuntimeStatus::Failed,
                ClientInitializationResult::RuntimeCancelled);
            return static_cast<DWORD>(ClientInitializationResult::RuntimeCancelled);
        }

        const auto mode = runtime->core.configuration.Mode();
        const bool appearance =
            mode == fable::automation::runtime::ClientMode::AppearanceCycle ||
            runtime->core.configuration.ScenarioIs(L"appearance_cycle");
        LogEvent(
            "ClientHooksReady",
            mode == fable::automation::runtime::ClientMode::TransformProbe
                ? "window-and-transform-probe"
                : appearance
                    ? "window-observation-and-angelscript-puppet"
                    : "window-observation-only");
        CompleteBootstrap(
            runtime->core,
            ClientRuntimeStatus::Ready,
            ClientInitializationResult::RuntimeReady);
        return static_cast<DWORD>(ClientInitializationResult::RuntimeReady);
    }
}
