#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Combat/CombatHealthMutationEvent.h"
#include "Game/Creature/Combat/Native/CombatHealthMutationFunction.h"

#include <Windows.h>

#include <array>
#include <atomic>

namespace fable::game::creature::combat
{
    class CombatHealthMutationHook final
    {
    public:
        using EventSink = void(*)(
            void* context,
            const CombatHealthMutationEvent& event);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        void SetEventSink(EventSink sink, void* context) noexcept;
        bool SetReplicaProtected(void* creature, bool protectedReplica) noexcept;
        bool ApplyAuthoritative(
            void* creature,
            float currentHealth,
            float maximumHealth) noexcept;
        // Applies one host-approved combat delta on the target owner and
        // emits the ordinary mutation event so EntityVitals remains the sole
        // reliable health publisher.
        bool ApplyOwnedCombatDamage(
            void* creature,
            float damage) noexcept;
        bool Read(
            void* creature,
            float& currentHealth,
            float& maximumHealth) const noexcept;
        [[nodiscard]] static bool IsProtectedReplica(
            void* creature) noexcept;
        // OnHit may resolve against a protected remote presentation. Preserve
        // the native attempted outcome for the enclosing hit observer while
        // restoring replica health immediately.
        static void ClearProtectedReplicaAttempt() noexcept;
        [[nodiscard]] static bool ConsumeProtectedReplicaAttempt(
            void* creature,
            float& currentHealth,
            float& maximumHealth) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static void __fastcall Intercept(
            void* creature,
            void* unused,
            float delta,
            bool combatFlag);
        static std::uint64_t ReadThingUid(void* creature) noexcept;
        [[nodiscard]] bool IsReplicaProtected(void* creature) const noexcept;

        static CombatHealthMutationHook* active_;
        native::CombatHealthMutationFunction::Pointer original_ = nullptr;
        void* trampoline_ = nullptr;
        std::uint8_t* target_ = nullptr;
        std::array<std::uint8_t,
            native::CombatHealthMutationFunction::DisplacedBytes>
                originalBytes_ = {};
        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::atomic<EventSink> eventSink_{nullptr};
        std::atomic<void*> eventSinkContext_{nullptr};
        std::atomic_uint observedCount_{0};
        static constexpr std::size_t ReplicaCapacity = 64;
        std::array<std::atomic<void*>, ReplicaCapacity> protectedReplicas_ = {};
        std::atomic_uint rejectedReplicaMutationCount_{0};
    };
}
