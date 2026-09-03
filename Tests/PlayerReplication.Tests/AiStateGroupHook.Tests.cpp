#include "Game/Creature/AI/Hooks/AiBrainUpdateObserver.h"
#include "Game/Creature/AI/Native/AiBrainFunctions.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

namespace
{
    int failures = 0;

    void Check(
        const bool condition,
        const char* expression,
        const char* test)
    {
        if (condition)
        {
            return;
        }
        ++failures;
        std::cerr << test << ": " << expression << '\n';
    }

#define CHECK(test, expression) Check((expression), #expression, (test))

    struct GateProbe final
    {
        bool allow = false;
        unsigned int calls = 0;
        void* creature = nullptr;
        int frameTime = 0;
        void* proposal = nullptr;
        HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        bool block = false;

        ~GateProbe()
        {
            CloseHandle(entered);
            CloseHandle(release);
        }

        static bool Invoke(
            void* context,
            void* creature,
            int frameTime,
            void* proposal)
        {
            auto& probe = *static_cast<GateProbe*>(context);
            ++probe.calls;
            probe.creature = creature;
            probe.frameTime = frameTime;
            probe.proposal = proposal;
            if (probe.block)
            {
                SetEvent(probe.entered);
                (void)WaitForSingleObject(probe.release, 5000);
            }
            return probe.allow;
        }
    };

    struct NativeImage final
    {
        std::uint8_t* base = nullptr;
        std::size_t size = 0;

        ~NativeImage()
        {
            // The production state-group hook intentionally has process
            // lifetime. Keep this synthetic image alive for the same reason.
        }

        bool Allocate()
        {
#if !defined(_M_IX86)
            return false;
#else
            constexpr std::size_t maxRva =
                fable::game::creature::ai::native::AiBrainFunctions::
                    StateGroupGlobalRva;
            size = maxRva + 0x1000;
            base = static_cast<std::uint8_t*>(VirtualAlloc(
                nullptr,
                size,
                MEM_RESERVE,
                PAGE_NOACCESS));
            return base != nullptr;
#endif
        }

        bool Commit(std::uintptr_t rva)
        {
            return VirtualAlloc(
                base + (rva & ~std::uintptr_t{0xFFF}),
                0x1000,
                MEM_COMMIT,
                PAGE_READWRITE) != nullptr;
        }

        void* At(std::uintptr_t rva) const
        {
            return base + rva;
        }
    };

    bool BuildImage(NativeImage& image)
    {
#if !defined(_M_IX86)
        (void)image;
        return false;
#else
        using Functions =
            fable::game::creature::ai::native::AiBrainFunctions;
        if (!image.Allocate() ||
            !image.Commit(Functions::VtableRva) ||
            !image.Commit(Functions::UpdateRva) ||
            !image.Commit(Functions::StateGroupDispatcherRva) ||
            !image.Commit(Functions::StateGroupGlobalRva) ||
            !image.Commit(Functions::StateGroupArbitrationRva))
        {
            return false;
        }

        auto* const update = static_cast<std::uint8_t*>(image.At(Functions::UpdateRva));
        *reinterpret_cast<void**>(image.At(Functions::VtableRva + 4 * sizeof(void*))) = update;
        constexpr std::array<std::uint8_t, 10> updatePrefix = {
            0x56, 0x8B, 0xF1, 0x8B, 0x4E,
            0x44, 0x80, 0x79, 0x10, 0x00};
        std::memcpy(update, updatePrefix.data(), updatePrefix.size());

        auto* const arbitration = static_cast<std::uint8_t*>(image.At(
            Functions::StateGroupArbitrationRva));
        constexpr std::array<std::uint8_t, 8> arbitrationCode = {
            0xB8, 0x01, 0x00, 0x00, 0x00, 0xC2, 0x08, 0x00};
        std::memcpy(arbitration, arbitrationCode.data(), arbitrationCode.size());

        auto* const dispatcher = static_cast<std::uint8_t*>(
            image.At(Functions::StateGroupDispatcherRva));
        const std::uint32_t global = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(image.At(
                Functions::StateGroupGlobalRva)));
        constexpr std::array<std::uint8_t, 21> dispatcherPrefix = {
            0x80, 0x3D, 0, 0, 0, 0, 0x00,
            0x75, 0x05, 0x32, 0xC0, 0xC2, 0x08, 0x00,
            0x8B, 0x89, 0xE0, 0x01, 0x00, 0x00, 0xE9};
        std::memcpy(dispatcher, dispatcherPrefix.data(), dispatcherPrefix.size());
        std::memcpy(dispatcher + 2, &global, sizeof(global));
        const auto jumpFrom = reinterpret_cast<std::uintptr_t>(dispatcher + 21);
        const auto jumpTo = reinterpret_cast<std::uintptr_t>(arbitration);
        const std::int32_t relative = static_cast<std::int32_t>(jumpTo - (jumpFrom + 4));
        std::memcpy(dispatcher + 21, &relative, sizeof(relative));

        *static_cast<std::uint8_t*>(image.At(Functions::StateGroupGlobalRva)) = 1;
        DWORD oldProtection = 0;
        return VirtualProtect(
            dispatcher,
            32,
            PAGE_EXECUTE_READ,
            &oldProtection) != FALSE &&
            VirtualProtect(update, 32, PAGE_EXECUTE_READ, &oldProtection) != FALSE &&
            VirtualProtect(arbitration, 32, PAGE_EXECUTE_READ, &oldProtection) != FALSE;
#endif
    }
}

int RunAiStateGroupHookTests()
{
#if !defined(_M_IX86)
    return 0;
#else
    using Functions = fable::game::creature::ai::native::AiBrainFunctions;
    using Dispatcher = Functions::StateGroupDecisionPointer;
    constexpr const char* test = "AI state-group hook ABI and detach";

    NativeImage image;
    const bool built = BuildImage(image);
    CHECK(test, built);
    if (!built || image.base == nullptr)
    {
        return failures;
    }

    void* target = nullptr;
    Dispatcher original = nullptr;
    CHECK(test, Functions::ResolveStateGroupDispatcher(
        reinterpret_cast<HMODULE>(image.base), &target, original));
    CHECK(test, target == image.At(Functions::StateGroupDispatcherRva));
    CHECK(test, original != nullptr);

    auto* const dispatcher = static_cast<std::uint8_t*>(target);
    const std::uint8_t saved = dispatcher[12];
    DWORD dispatcherProtection = 0;
    CHECK(test, VirtualProtect(
        dispatcher,
        32,
        PAGE_EXECUTE_READWRITE,
        &dispatcherProtection) != FALSE);
    dispatcher[12] = 0x04;
    CHECK(test, !Functions::ResolveStateGroupDispatcher(
        reinterpret_cast<HMODULE>(image.base), &target, original));
    dispatcher[12] = saved;
    DWORD ignoredProtection = 0;
    CHECK(test, VirtualProtect(
        dispatcher,
        32,
        PAGE_EXECUTE_READ,
        &ignoredProtection) != FALSE);
    CHECK(test, Functions::ResolveStateGroupDispatcher(
        reinterpret_cast<HMODULE>(image.base), &target, original));
    if (original == nullptr || target == nullptr) return failures + 1;

    std::array<std::uint8_t, 0x240> creatureBytes = {};
    void* const creature = creatureBytes.data();
    *reinterpret_cast<void**>(creatureBytes.data() + 0x1E0) = image.At(
        Functions::StateGroupArbitrationRva);
    int proposalValue = 7;
    CHECK(test, original(creature, 11, &proposalValue));
    CHECK(test, proposalValue == 7);

    fable::game::creature::ai::AiBrainUpdateObserver observer;
    CHECK(test, observer.Install(reinterpret_cast<HMODULE>(image.base), {}));
    CHECK(test, observer.IsInstalled());

    GateProbe gate;
    observer.SetStateGroupExecutionSink(&GateProbe::Invoke, &gate);
    proposalValue = 19;
    const Dispatcher hooked = reinterpret_cast<Dispatcher>(target);
    CHECK(test, !hooked(creature, 22, &proposalValue));
    CHECK(test, gate.calls == 1 && gate.creature == creature &&
        gate.frameTime == 22 && gate.proposal == &proposalValue);
    CHECK(test, proposalValue == 19);

    gate.allow = true;
    CHECK(test, hooked(creature, 23, &proposalValue));
    CHECK(test, gate.calls == 2);

    CHECK(test, gate.entered != nullptr && gate.release != nullptr);
    gate.block = true;
    ResetEvent(gate.entered);
    ResetEvent(gate.release);
    std::thread inFlight([&]
    {
        (void)hooked(creature, 24, &proposalValue);
    });
    CHECK(test, WaitForSingleObject(gate.entered, 5000) == WAIT_OBJECT_0);
    HANDLE detached = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CHECK(test, detached != nullptr);
    std::thread stop([&]
    {
        observer.Shutdown();
        if (detached != nullptr)
        {
            SetEvent(detached);
        }
    });
    if (detached != nullptr)
    {
        CHECK(test, WaitForSingleObject(detached, 50) == WAIT_TIMEOUT);
    }
    SetEvent(gate.release);
    inFlight.join();
    stop.join();
    if (detached != nullptr)
    {
        CHECK(test, WaitForSingleObject(detached, 0) == WAIT_OBJECT_0);
    }
    if (detached != nullptr)
    {
        CloseHandle(detached);
    }
    CHECK(test, !observer.IsInstalled());
    CHECK(test, hooked(creature, 25, &proposalValue));
    CHECK(test, gate.calls == 3);
    return failures;
#endif
}
