#include "Core/Bootstrap/ClientRuntime.h"

#include "Core/Bootstrap/ClientRuntimeServices.h"
#include "Core/Bootstrap/ClientRuntimeStartup.h"
#include "Core/Bootstrap/FeatureRegistry.h"

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <new>
#include <string>

#if defined(_M_IX86)
// CreateRemoteThread resolves exports by their stable public names. Keep the
// stdcall implementation ABI while publishing undecorated x86 aliases.
#pragma comment(linker, "/EXPORT:AlbionTogetherInitialize=_AlbionTogetherInitialize@4")
#pragma comment(linker, "/EXPORT:AlbionTogetherWaitForReady=_AlbionTogetherWaitForReady@4")
#pragma comment(linker, "/EXPORT:AlbionTogetherShutdown=_AlbionTogetherShutdown@0")
#endif

namespace
{
    using namespace fable::core::bootstrap;

    std::atomic<HMODULE> g_capturedClientModule{nullptr};
    std::atomic<ClientRuntimeContext*> g_runtimeContext{nullptr};
    SRWLOCK g_lifecycleLock = SRWLOCK_INIT;

    ClientRuntimeContext& RuntimeContext() noexcept
    {
        return *g_runtimeContext.load(std::memory_order_acquire);
    }

    void CloseRuntimeHandles(CoreRuntimeContext& core) noexcept
    {
        if (core.bootstrapThread != nullptr)
        {
            CloseHandle(core.bootstrapThread);
            core.bootstrapThread = nullptr;
        }
        if (core.cancelEvent != nullptr)
        {
            CloseHandle(core.cancelEvent);
            core.cancelEvent = nullptr;
        }
        if (core.resumeEvent != nullptr)
        {
            CloseHandle(core.resumeEvent);
            core.resumeEvent = nullptr;
        }
        if (core.completionEvent != nullptr)
        {
            CloseHandle(core.completionEvent);
            core.completionEvent = nullptr;
        }
    }
}

namespace fable::core::bootstrap
{
    CoreRuntimeContext& CoreContext() noexcept { return RuntimeContext().core; }
    DiagnosticsRuntimeContext& DiagnosticsContext() noexcept { return RuntimeContext().diagnostics; }
    NativeHooksRuntimeContext& NativeHooksContext() noexcept { return RuntimeContext().nativeHooks; }
    GameplayRuntimeContext& GameplayContext() noexcept { return RuntimeContext().gameplay; }
    AutomationRuntimeContext& AutomationContext() noexcept { return RuntimeContext().automation; }
    CharacterSnapshotRuntimeContext& CharacterSnapshotContext() noexcept
    {
        return RuntimeContext().automation.characterSnapshot;
    }
    FrontEndAutomationRuntimeContext& FrontEndContext() noexcept
    {
        return RuntimeContext().automation.frontEnd;
    }
    TransformProbeRuntimeContext& TransformContext() noexcept
    {
        return RuntimeContext().automation.transformProbe;
    }
    UiRuntimeContext& UiContext() noexcept { return RuntimeContext().ui; }

    FeatureLifecycleContext& FeatureLifecycle(FeatureContext& context) noexcept
    {
        return *static_cast<FeatureLifecycleContext*>(context.userData);
    }

    const FeatureLifecycleContext& FeatureLifecycle(const FeatureContext& context) noexcept
    {
        return *static_cast<const FeatureLifecycleContext*>(context.userData);
    }

    bool IsPreResumeStage(const FeatureContext& context) noexcept
    {
        return FeatureLifecycle(context).stage == FeatureInstallStage::PreResume;
    }

    bool ScenarioIs(const wchar_t* value) { return CoreContext().configuration.ScenarioIs(value); }
    bool ScenarioUsesFrontEndStartAutomation() { return CoreContext().configuration.UsesFrontEndStartAutomation(); }
    bool ScenarioLoadsFixture() { return CoreContext().configuration.LoadsFixture(); }
    void Log(const char* message) { CoreContext().diagnosticLog.Log(message); }

    void LogFormat(const char* format, ...)
    {
        char message[1024] = {};
        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(message, std::size(message), format, arguments);
        va_end(arguments);
        Log(message);
    }

    void LogEvent(const char* state, const char* detail)
    {
        CoreContext().diagnosticLog.Event(state, detail);
    }

    void ScriptLog(const char* message)
    {
        LogFormat("Script: %s", message != nullptr ? message : "<null>");
    }

    void ScriptEvent(const char* state, const char* detail)
    {
        AutomationContext().appearanceCycle.ObserveScriptEvent(state);
        LogEvent(state != nullptr ? state : "ScriptEvent", detail != nullptr ? detail : "");
    }

    std::string WideToUtf8(const wchar_t* value)
    {
        if (value == nullptr || *value == L'\0')
        {
            return {};
        }
        const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return {};
        }
        std::string converted(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value, -1, converted.data(), required, nullptr, nullptr);
        converted.pop_back();
        return converted;
    }

    void CaptureClientModule(HMODULE module) noexcept
    {
        g_capturedClientModule.store(module, std::memory_order_release);
    }

    ClientInitializationResult InitializeClientRuntime() noexcept
    {
        AcquireSRWLockExclusive(&g_lifecycleLock);
        ClientRuntimeContext* existing = g_runtimeContext.load(std::memory_order_acquire);
        if (existing != nullptr)
        {
            const ClientRuntimeStatus status = existing->core.status.load(std::memory_order_acquire);
            ReleaseSRWLockExclusive(&g_lifecycleLock);
            if (status == ClientRuntimeStatus::Ready)
            {
                return ClientInitializationResult::AlreadyReady;
            }
            return status == ClientRuntimeStatus::PreResumeReady || status == ClientRuntimeStatus::Starting
                ? ClientInitializationResult::PreResumeReady
                : ClientInitializationResult::InvalidState;
        }

        const HMODULE clientModule = g_capturedClientModule.load(std::memory_order_acquire);
        if (clientModule == nullptr)
        {
            ReleaseSRWLockExclusive(&g_lifecycleLock);
            return ClientInitializationResult::InvalidModule;
        }
        auto* runtime = new (std::nothrow) ClientRuntimeContext();
        if (runtime == nullptr)
        {
            ReleaseSRWLockExclusive(&g_lifecycleLock);
            return ClientInitializationResult::AllocationFailed;
        }
        runtime->core.clientModule = clientModule;
        runtime->core.gameModule = GetModuleHandleW(nullptr);
        runtime->core.configuration.LoadFromEnvironment();
        runtime->core.cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        runtime->core.resumeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        runtime->core.completionEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (runtime->core.cancelEvent == nullptr || runtime->core.resumeEvent == nullptr ||
            runtime->core.completionEvent == nullptr)
        {
            CloseRuntimeHandles(runtime->core);
            delete runtime;
            ReleaseSRWLockExclusive(&g_lifecycleLock);
            return ClientInitializationResult::EventCreationFailed;
        }
        runtime->preResumeLifecycle = {runtime, FeatureInstallStage::PreResume};
        runtime->postResumeLifecycle = {runtime, FeatureInstallStage::PostResume};
        runtime->preResumeFeatureContext.userData = &runtime->preResumeLifecycle;
        runtime->postResumeFeatureContext.userData = &runtime->postResumeLifecycle;
        g_runtimeContext.store(runtime, std::memory_order_release);

        FeaturePlan plan{};
        if (!BuildLinkerFeaturePlan(runtime->preResumeFeatureContext, plan))
        {
            g_runtimeContext.store(nullptr, std::memory_order_release);
            CloseRuntimeHandles(runtime->core);
            delete runtime;
            ReleaseSRWLockExclusive(&g_lifecycleLock);
            return ClientInitializationResult::FeaturePlanFailed;
        }
        FeatureInstallFailure failure{};
        if (!runtime->preResumeFeatures.Install(plan, runtime->preResumeFeatureContext, failure))
        {
            LogFormat("Startup: pre-resume feature failed; feature=%s identity=%s.",
                failure.featureId != nullptr ? failure.featureId : "<unknown>",
                failure.failureIdentity != nullptr ? failure.failureIdentity : "<unknown>");
            runtime->core.failureCode.store(
                static_cast<DWORD>(ClientInitializationResult::PreResumeFeatureFailed),
                std::memory_order_release);
            runtime->preResumeFeatures.Shutdown();
            g_runtimeContext.store(nullptr, std::memory_order_release);
            CloseRuntimeHandles(runtime->core);
            delete runtime;
            ReleaseSRWLockExclusive(&g_lifecycleLock);
            return ClientInitializationResult::PreResumeFeatureFailed;
        }
        runtime->core.bootstrapThread = CreateThread(
            nullptr, 0, BootstrapThread, runtime, 0, &runtime->core.bootstrapThreadId);
        if (runtime->core.bootstrapThread == nullptr)
        {
            runtime->preResumeFeatures.Shutdown();
            g_runtimeContext.store(nullptr, std::memory_order_release);
            CloseRuntimeHandles(runtime->core);
            delete runtime;
            ReleaseSRWLockExclusive(&g_lifecycleLock);
            return ClientInitializationResult::BootstrapThreadFailed;
        }
        runtime->core.status.store(ClientRuntimeStatus::PreResumeReady, std::memory_order_release);
        ReleaseSRWLockExclusive(&g_lifecycleLock);
        return ClientInitializationResult::PreResumeReady;
    }

    ClientInitializationResult WaitForClientRuntime(const DWORD timeoutMilliseconds) noexcept
    {
        AcquireSRWLockShared(&g_lifecycleLock);
        ClientRuntimeContext* runtime = g_runtimeContext.load(std::memory_order_acquire);
        if (runtime == nullptr)
        {
            ReleaseSRWLockShared(&g_lifecycleLock);
            return ClientInitializationResult::InvalidState;
        }
        HANDLE completion = nullptr;
        if (!DuplicateHandle(
                GetCurrentProcess(),
                runtime->core.completionEvent,
                GetCurrentProcess(),
                &completion,
                SYNCHRONIZE,
                FALSE,
                0))
        {
            ReleaseSRWLockShared(&g_lifecycleLock);
            return ClientInitializationResult::RuntimeFailed;
        }
        SetEvent(runtime->core.resumeEvent);
        ReleaseSRWLockShared(&g_lifecycleLock);
        const DWORD wait = WaitForSingleObject(completion, timeoutMilliseconds);
        CloseHandle(completion);
        if (wait == WAIT_TIMEOUT)
        {
            return ClientInitializationResult::RuntimeTimeout;
        }
        if (wait != WAIT_OBJECT_0)
        {
            return ClientInitializationResult::RuntimeFailed;
        }

        AcquireSRWLockShared(&g_lifecycleLock);
        if (g_runtimeContext.load(std::memory_order_acquire) != runtime)
        {
            ReleaseSRWLockShared(&g_lifecycleLock);
            return ClientInitializationResult::RuntimeCancelled;
        }
        if (runtime->core.status.load(std::memory_order_acquire) == ClientRuntimeStatus::Ready)
        {
            ReleaseSRWLockShared(&g_lifecycleLock);
            return ClientInitializationResult::RuntimeReady;
        }
        const DWORD failure = runtime->core.failureCode.load(std::memory_order_acquire);
        const ClientInitializationResult result = failure != ERROR_SUCCESS
            ? static_cast<ClientInitializationResult>(failure)
            : ClientInitializationResult::RuntimeFailed;
        ReleaseSRWLockShared(&g_lifecycleLock);
        return result;
    }

    void ShutdownClientRuntime() noexcept
    {
        AcquireSRWLockExclusive(&g_lifecycleLock);
        ClientRuntimeContext* runtime = g_runtimeContext.load(std::memory_order_acquire);
        if (runtime == nullptr)
        {
            ReleaseSRWLockExclusive(&g_lifecycleLock);
            return;
        }
        runtime->core.status.store(ClientRuntimeStatus::Stopping, std::memory_order_release);
        SetEvent(runtime->core.cancelEvent);
        SetEvent(runtime->core.resumeEvent);
        if (runtime->core.bootstrapThreadId == GetCurrentThreadId())
        {
            ReleaseSRWLockExclusive(&g_lifecycleLock);
            return;
        }
        if (runtime->core.bootstrapThread != nullptr)
        {
            WaitForSingleObject(runtime->core.bootstrapThread, INFINITE);
        }
        runtime->postResumeFeatures.Shutdown();
        runtime->preResumeFeatures.Shutdown();
        runtime->core.status.store(ClientRuntimeStatus::Stopped, std::memory_order_release);
        g_runtimeContext.store(nullptr, std::memory_order_release);
        CloseRuntimeHandles(runtime->core);
        delete runtime;
        ReleaseSRWLockExclusive(&g_lifecycleLock);
    }
}

extern "C" __declspec(dllexport) DWORD WINAPI AlbionTogetherInitialize(void*)
{
    return static_cast<DWORD>(fable::core::bootstrap::InitializeClientRuntime());
}

extern "C" __declspec(dllexport) DWORD WINAPI AlbionTogetherWaitForReady(void* timeoutMilliseconds)
{
    return static_cast<DWORD>(fable::core::bootstrap::WaitForClientRuntime(
        static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(timeoutMilliseconds))));
}

extern "C" __declspec(dllexport) void WINAPI AlbionTogetherShutdown()
{
    fable::core::bootstrap::ShutdownClientRuntime();
}
