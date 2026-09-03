#include "SimulationFrameHook.h"

#include "Core/GameThread/Native/SimulationFrameFunction.h"
#include "Core/Hooking/CodePatch.h"

#include <array>
#include <cstdio>
#include <mutex>

namespace fable::core::game_thread
{
    namespace
    {
        using Frame = void (__thiscall*)(void*);
        struct HookState final
        {
            hooking::InlineHook patch;
            Frame original = nullptr;
            HMODULE gameModule = nullptr;
            std::mutex consumerLock;
            SimulationFrameHook::Sink sink = nullptr;
            void* context = nullptr;
            Diagnostics diagnostics = {};
            DWORD ownerThread = 0;
            unsigned int slowFramesReported = 0;
            bool threadMismatchReported = false;
        };

        // Deliberately bounded to one allocation for the process. Native calls
        // may still be inside the trampoline after the mod consumer detaches.
        HookState& State()
        {
            static HookState* const state = new HookState;
            return *state;
        }

        bool Validate(HMODULE module) noexcept
        {
            using Layout = native::SimulationFrameFunction;
            if (module == nullptr) return false;
            const auto base = reinterpret_cast<std::uintptr_t>(module);
            std::array<std::uint8_t, sizeof(Layout::Prefix)> prefix{};
            std::array<std::uint8_t, 3> epilogue{};
            std::array<std::uint8_t, 5> caller{};
            return ReadProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<void*>(base + Layout::AddressRva),
                    prefix.data(), prefix.size(), nullptr) &&
                ReadProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<void*>(base + Layout::AddressRva + Layout::ReturnOffset),
                    epilogue.data(), epilogue.size(), nullptr) &&
                ReadProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<void*>(base + Layout::CallerRva),
                    caller.data(), caller.size(), nullptr) &&
                Layout::Matches(prefix.data(), epilogue.data(), caller.data());
        }
    }

    bool SimulationFrameHook::InstallBeforeResume(HMODULE gameModule) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        return false;
#else
        auto& state = State();
        if (state.patch.IsInstalled()) return state.gameModule == gameModule;
        if (!Validate(gameModule)) return false;
        HMODULE pinnedModule = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&Observe), &pinnedModule)) return false;
        using Layout = native::SimulationFrameFunction;
        void* const target = reinterpret_cast<void*>(
            reinterpret_cast<std::uintptr_t>(gameModule) + Layout::AddressRva);
        if (!state.patch.Install(target, Layout::Prefix, Layout::DisplacedBytes,
                reinterpret_cast<void*>(&Observe), Layout::DisplacedBytes)) return false;
        state.original = reinterpret_cast<Frame>(state.patch.Original());
        state.gameModule = gameModule;
        return true;
#endif
    }

    bool SimulationFrameHook::Enable(Sink sink, void* context,
        const Diagnostics& diagnostics) noexcept
    {
        auto& state = State();
        if (state.original == nullptr || sink == nullptr || context == nullptr) return false;
        std::lock_guard<std::mutex> lock(state.consumerLock);
        const bool available = state.sink == nullptr;
        if (available)
        {
            state.context = context;
            state.diagnostics = diagnostics;
            state.ownerThread = 0;
            state.slowFramesReported = 0;
            state.threadMismatchReported = false;
            state.sink = sink;
        }
        return available;
    }

    void SimulationFrameHook::Disable() noexcept
    {
        auto& state = State();
        std::lock_guard<std::mutex> lock(state.consumerLock);
        state.sink = nullptr;
        state.context = nullptr;
        state.diagnostics = {};
    }

    void __fastcall SimulationFrameHook::Observe(void* application, void*)
    {
        auto& state = State();
        const ULONGLONG begin = GetTickCount64();
        state.original(application);
        const ULONGLONG nativeEnd = GetTickCount64();
        static thread_local bool dispatching = false;
        if (dispatching) return; // A scripted native operation may re-enter the loop.

        // Exclusive here serializes unexpected additional callers as well as
        // Disable. Never hold this lock while executing the native original.
        std::lock_guard<std::mutex> lock(state.consumerLock);
        if (state.sink != nullptr)
        {
            const DWORD thread = GetCurrentThreadId();
            if (state.ownerThread == 0)
            {
                state.ownerThread = thread;
                state.diagnostics.Event("NativeSimulationDispatchReady",
                    "gameplay mutations run after the native simulation frame; window callbacks only enqueue requests");
            }
            if (state.ownerThread == thread)
            {
                struct DispatchScope final
                {
                    bool& flag;
                    explicit DispatchScope(bool& value) : flag(value) { flag = true; }
                    ~DispatchScope() { flag = false; }
                } scope(dispatching);
                state.sink(state.context);
                const ULONGLONG end = GetTickCount64();
                if ((nativeEnd - begin >= 1000 || end - nativeEnd >= 1000) &&
                    state.slowFramesReported++ < 32)
                {
                    char detail[160] = {};
                    std::snprintf(detail, sizeof(detail),
                        "thread=%lu native_ms=%llu mod_ms=%llu",
                        thread, nativeEnd - begin, end - nativeEnd);
                    state.diagnostics.Event("NativeSimulationSlowFrame", detail);
                }
            }
            else if (!state.threadMismatchReported)
            {
                state.threadMismatchReported = true;
                state.diagnostics.Event("NativeSimulationThreadRejected",
                    "frame arrived on another thread; no gameplay mutation was dispatched");
            }
        }
    }
}
