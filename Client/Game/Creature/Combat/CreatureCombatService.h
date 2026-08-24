#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Combat/Hooks/PlayerAttackAbilityHook.h"
#include "Game/Creature/Combat/Hooks/CombatHealthMutationHook.h"
#include "Game/Creature/Combat/CreatureHitResolutionHook.h"
#include "Game/Creature/Combat/CreatureAbilityEvent.h"
#include "Game/Creature/Animation/Hooks/CreatureActionAnimationSelectionHook.h"

#include <Windows.h>

#include <array>
#include <atomic>

namespace fable::game
{
    class Entity;
    class EntityService;
}

namespace fable::game::creature::animation
{
    class CreatureAnimationService;
}

namespace fable::game::creature::combat
{
    class CreatureCombatService final
    {
    public:
        using AbilitySink = void(*)(
            void* context,
            const CreatureAbilityEvent& event);
        using HealthMutationSink = CombatHealthMutationHook::EventSink;
        using ResolvedHitSink = CreatureHitResolutionHook::EventSink;

        ~CreatureCombatService();

        bool Initialize(
            EntityService& entities,
            animation::CreatureAnimationService& animation,
            const core::Diagnostics& diagnostics);
        bool AttachActionLifecycleObserver(
            actions::CreatureActionLifecycleObserver& observer) noexcept;
        void DetachActionLifecycleObserver() noexcept;
        void Shutdown() noexcept;
        bool RoutePlayerCombat(Entity* hero, Entity* puppet);
        bool ResolvePlayerAttackCreature(
            void* sourceCreature,
            void*& routedCreature) noexcept;
        void ObservePlayerAbility(
            void* sourceCreature,
            unsigned int abilityId,
            float charge,
            bool attackCommand) noexcept;
        bool AddAbilitySink(
            AbilitySink sink,
            void* context) noexcept;
        void RemoveAbilitySink(
            AbilitySink sink,
            void* context) noexcept;
        bool SubmitReplicatedAbility(
            void* creature,
            unsigned int abilityId,
            float charge) noexcept;
        bool SubmitReplicatedAbility(
            void* creature,
            unsigned int abilityId,
            float charge,
            const char* resolvedActionType,
            std::uint32_t resolvedAnimationId) noexcept;
        bool SubmitReplicatedImmediateAttack(
            void* creature,
            void* targetCreature) noexcept;
        bool SubmitReplicatedHitReaction(
            void* target,
            void* source,
            const float (&position)[3],
            const float (&direction)[3],
            bool knockdown) noexcept;
        bool SubmitReplicatedHitReaction(
            void* target,
            void* source,
            const float (&position)[3],
            const float (&direction)[3],
            bool knockdown,
            std::uint32_t resolvedAnimationId) noexcept;
        bool SubmitReplicatedDeath(void* creature) noexcept;
        // Submits a new locally-authoritative creature attack through Fable's
        // normal action boundary. Unlike replicated replay, this is observed
        // and published by the entity-action replication layer.
        bool SubmitAuthoritativeImmediateAttack(
            void* creature,
            void* targetCreature) noexcept;
        bool SubmitReplicatedUntargetedAttack(
            void* creature,
            const float (&targetPosition)[3]) noexcept;
        bool SubmitReplicatedUntargetedAttack(
            void* creature,
            const float (&targetPosition)[3],
            const char* resolvedActionType,
            std::uint32_t resolvedAnimationId) noexcept;
        void SetHealthMutationSink(
            HealthMutationSink sink,
            void* context) noexcept;
        bool AddResolvedHitSink(
            ResolvedHitSink sink,
            void* context) noexcept;
        void RemoveResolvedHitSink(
            ResolvedHitSink sink,
            void* context) noexcept;
        bool SetReplicaHealthProtection(
            void* creature,
            bool protectedReplica) noexcept;
        bool ReadCombatHealth(
            void* creature,
            float& currentHealth,
            float& maximumHealth) const noexcept;
        bool ApplyAuthoritativeCombatHealth(
            void* creature,
            float currentHealth,
            float maximumHealth) noexcept;
        bool ApplyOwnedCombatDamage(
            void* creature,
            float damage) noexcept;
        void ClearPlayerCombat() noexcept;

        [[nodiscard]] bool IsPlayerCombatRouted() const noexcept;
        [[nodiscard]] unsigned int RoutedPlayerAttackCount() const noexcept;
        [[nodiscard]] unsigned int InterceptedHeroAttackCount() const noexcept;

    private:
        static void CaptureResolvedHit(
            void* context,
            const ResolvedHitEvent& event) noexcept;

        EntityService* entities_ = nullptr;
        animation::CreatureAnimationService* animation_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        mutable SRWLOCK routeLock_ = SRWLOCK_INIT;
        mutable SRWLOCK abilitySinkLock_ = SRWLOCK_INIT;
        mutable SRWLOCK resolvedHitSinkLock_ = SRWLOCK_INIT;
        Entity* retainedHero_ = nullptr;
        Entity* retainedPuppet_ = nullptr;
        PlayerAttackAbilityHook playerAttackAbilityHook_;
        CombatHealthMutationHook combatHealthMutationHook_;
        CreatureHitResolutionHook creatureHitResolutionHook_;
        animation::CreatureActionAnimationSelectionHook
            actionAnimationSelectionHook_;
        struct AbilitySinkEntry final
        {
            AbilitySink sink = nullptr;
            void* context = nullptr;
        };
        static constexpr std::size_t AbilitySinkCapacity = 4;
        std::array<AbilitySinkEntry, AbilitySinkCapacity> abilitySinks_ = {};
        struct ResolvedHitSinkEntry final
        {
            ResolvedHitSink sink = nullptr;
            void* context = nullptr;
        };
        static constexpr std::size_t ResolvedHitSinkCapacity = 4;
        std::array<ResolvedHitSinkEntry, ResolvedHitSinkCapacity>
            resolvedHitSinks_ = {};
        std::atomic_uint routedAttackCount_{0};
        std::atomic_uint observedPlayerAttackCount_{0};
    };
}
