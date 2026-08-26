#pragma once

#include <Windows.h>

namespace fable::core::bootstrap
{
    enum class ClientInitializationResult : DWORD
    {
        PreResumeReady = 0x0000F101,
        RuntimeReady = 0x0000F102,
        AlreadyReady = 0x0000F103,
        InvalidModule = 0xE001F001,
        AllocationFailed = 0xE001F002,
        EventCreationFailed = 0xE001F003,
        FeaturePlanFailed = 0xE001F004,
        PreResumeFeatureFailed = 0xE001F005,
        BootstrapThreadFailed = 0xE001F006,
        RuntimeFeatureFailed = 0xE001F007,
        RuntimeFailed = 0xE001F008,
        RuntimeTimeout = 0xE001F009,
        RuntimeCancelled = 0xE001F00A,
        InvalidState = 0xE001F00B,
    };

    // Captures only the module handle during DLL_PROCESS_ATTACH. No hooks,
    // threads, logging, or environment work is performed by this function.
    void CaptureClientModule(HMODULE module) noexcept;

    // Starts the post-load client initialization while the game is still
    // suspended. The launcher calls the exported wrapper below remotely.
    [[nodiscard]] ClientInitializationResult InitializeClientRuntime() noexcept;
    [[nodiscard]] ClientInitializationResult WaitForClientRuntime(
        DWORD timeoutMilliseconds) noexcept;

    // Idempotent shutdown used by diagnostics/tests and future process-owned
    // shutdown coordination. The caller must first quiesce every game thread
    // that can execute an installed hook; joining the bootstrap/network work
    // does not make native game call sites safe to rewrite. The production
    // launcher therefore does not hot-unload the client. Normal process exit
    // lets Windows reclaim it, and DLL detach does not call this function.
    void ShutdownClientRuntime() noexcept;
}

extern "C" __declspec(dllexport) DWORD WINAPI FableTogetherInitialize(void*);
extern "C" __declspec(dllexport) DWORD WINAPI FableTogetherWaitForReady(
    void* timeoutMilliseconds);
extern "C" __declspec(dllexport) void WINAPI FableTogetherShutdown();
