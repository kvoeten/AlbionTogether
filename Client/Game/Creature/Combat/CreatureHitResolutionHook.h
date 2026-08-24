#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Combat/Native/CreatureHitResolutionFunction.h"
#include "Game/Creature/Combat/ResolvedHitEvent.h"

#include <Windows.h>

#include <array>
#include <atomic>

namespace fable::game::creature::combat
{
    class CreatureHitResolutionHook final
    {
    public:
        using EventSink = void(*)(void* context, const ResolvedHitEvent& event);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        void SetEventSink(EventSink sink, void* context) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static void __fastcall Intercept(
            void* creature,
            void* unused,
            void* hitParameters);
        static std::uint64_t ReadThingUid(void* thing) noexcept;
        static bool ReadHealth(
            void* creature,
            float& currentHealth,
            float& maximumHealth) noexcept;
        static bool ReadHitParameters(
            void* hitParameters,
            ResolvedHitEvent& event,
            void*& sourceThing) noexcept;

        static CreatureHitResolutionHook* active_;
        native::CreatureHitResolutionFunction::Pointer original_ = nullptr;
        void* trampoline_ = nullptr;
        std::uint8_t* target_ = nullptr;
        std::array<std::uint8_t,
            native::CreatureHitResolutionFunction::DisplacedBytes>
                originalBytes_ = {};
        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::atomic<EventSink> eventSink_{nullptr};
        std::atomic<void*> eventSinkContext_{nullptr};
        std::atomic_uint observedCount_{0};
        std::atomic_uint rejectedReplicaHitCount_{0};
        std::atomic_uint suppressedReplayHitCount_{0};
    };
}
