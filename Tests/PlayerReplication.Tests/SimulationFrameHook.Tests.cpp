#include "Core/GameThread/Hooks/SimulationFrameHook.h"
#include "Core/GameThread/Native/SimulationFrameFunction.h"

#include <algorithm>
#include <atomic>
#include <thread>

namespace
{
    struct CallbackProbe final
    {
        HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::atomic_uint calls{0};
        std::atomic_uint failures{0};
        unsigned int* nativeCalls = nullptr;

        ~CallbackProbe() { CloseHandle(entered); CloseHandle(release); }
        static void Invoke(void* context)
        {
            auto& probe = *static_cast<CallbackProbe*>(context);
            // The mock original increments before our callback can execute.
            if (*probe.nativeCalls == 0) ++probe.failures;
            ++probe.calls;
            SetEvent(probe.entered);
            if (WaitForSingleObject(probe.release, 5000) != WAIT_OBJECT_0) ++probe.failures;
        }
    };
}

int RunSimulationFrameHookTests()
{
    using Hook = fable::core::game_thread::SimulationFrameHook;
    using Layout = fable::core::game_thread::native::SimulationFrameFunction;
    using NativeFrame = void (__thiscall*)(void*);

    // Reserve the retail RVA span, committing only the two code pages. Like
    // the production hook, these pages live until process exit: freeing them
    // while the process-lifetime trampoline remains installed is forbidden.
    auto* const image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr,
        Layout::CallerRva + 4096, MEM_RESERVE, PAGE_NOACCESS));
    if (image == nullptr) return 1;
    const auto commitPage = [image](std::uintptr_t rva) {
        return VirtualAlloc(image + (rva & ~std::uintptr_t{4095}),
            4096, MEM_COMMIT, PAGE_READWRITE) != nullptr;
    };
    if (!commitPage(Layout::AddressRva) || !commitPage(Layout::CallerRva)) return 1;
    auto* const target = image + Layout::AddressRva;
    std::fill(target, target + Layout::ReturnOffset, std::uint8_t{0x90});
    std::copy(std::begin(Layout::Prefix), std::end(Layout::Prefix), target);
    target[8] = 0xFF; target[9] = 0x01; // inc dword ptr [ecx]
    constexpr std::uint8_t epilogue[] = {0x5E, 0xC9, 0xC3};
    constexpr std::uint8_t caller[] = {0xE8, 0x9F, 0xD0, 0xFF, 0xFF};
    std::copy(std::begin(epilogue), std::end(epilogue), target + Layout::ReturnOffset);
    std::copy(std::begin(caller), std::end(caller), image + Layout::CallerRva);
    DWORD oldProtection = 0;
    if (!VirtualProtect(target, Layout::ReturnOffset + sizeof(epilogue),
            PAGE_EXECUTE_READ, &oldProtection)) return 1;
    FlushInstructionCache(GetCurrentProcess(), target, Layout::ReturnOffset + sizeof(epilogue));

    if (!Hook::InstallBeforeResume(reinterpret_cast<HMODULE>(image))) return 1;
    const auto nativeFrame = reinterpret_cast<NativeFrame>(target);
    unsigned int nativeCalls = 0;
    CallbackProbe probe;
    probe.nativeCalls = &nativeCalls;
    if (probe.entered == nullptr || probe.release == nullptr) return 1;
    if (!Hook::Enable(&CallbackProbe::Invoke, &probe, {})) return 1;
    int failures = 0;
    std::thread native([&] { nativeFrame(&nativeCalls); });
    failures += WaitForSingleObject(probe.entered, 5000) != WAIT_OBJECT_0;

    HANDLE const detached = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread stop([&] { Hook::Disable(); SetEvent(detached); });
    // Consumer detach must not return while its callback is still in flight.
    failures += WaitForSingleObject(detached, 50) != WAIT_TIMEOUT;
    SetEvent(probe.release);
    native.join();
    stop.join();
    failures += WaitForSingleObject(detached, 0) != WAIT_OBJECT_0;
    CloseHandle(detached);
    failures += probe.calls.load() != 1 || nativeCalls != 1;

    nativeFrame(&nativeCalls); // Disabled hook is still a valid native passthrough.
    failures += probe.calls.load() != 1 || nativeCalls != 2;
    failures += !Hook::Enable(&CallbackProbe::Invoke, &probe, {});
    nativeFrame(&nativeCalls); // A new consumer can bind after orderly detach.
    failures += probe.calls.load() != 2 || nativeCalls != 3;
    Hook::Disable();
    failures += static_cast<int>(probe.failures.load());
    return failures;
}
