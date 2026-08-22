#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Combat/Hooks/PlayerAttackAbilityHook.h"
#include "Game/Creature/Combat/Hooks/CombatHealthMutationHook.h"
#include "Game/Creature/Combat/CreatureAbilityEvent.h"

#include <Windows.h>

#include <array>
#include <atomic>

namespace fable::game
{
    class Entity;
    class EntityService;
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

        ~CreatureCombatService();

        bool Initialize(
            EntityService& entities,
            const core::Diagnostics& diagnostics);
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
        bool SubmitReplicatedImmediateAttack(
            void* creature,
            void* targetCreature) noexcept;
        // Submits a new locally-authoritative creature attack through Fable's
        // normal action boundary. Unlike replicated replay, this is observed
        // and published by the entity-action replication layer.
        bool SubmitAuthoritativeImmediateAttack(
            void* creature,
            void* targetCreature) noexcept;
        bool SubmitReplicatedUntargetedAttack(
            void* creature,
            const float (&targetPosition)[3]) noexcept;
        void SetHealthMutationSink(
            HealthMutationSink sink,
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
        void ClearPlayerCombat() noexcept;

        [[nodiscard]] bool IsPlayerCombatRouted() const noexcept;
        [[nodiscard]] unsigned int RoutedPlayerAttackCount() const noexcept;
        [[nodiscard]] unsigned int InterceptedHeroAttackCount() const noexcept;

    private:
        EntityService* entities_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        mutable SRWLOCK routeLock_ = SRWLOCK_INIT;
        mutable SRWLOCK abilitySinkLock_ = SRWLOCK_INIT;
        Entity* retainedHero_ = nullptr;
        Entity* retainedPuppet_ = nullptr;
        PlayerAttackAbilityHook playerAttackAbilityHook_;
        CombatHealthMutationHook combatHealthMutationHook_;
        struct AbilitySinkEntry final
        {
            AbilitySink sink = nullptr;
            void* context = nullptr;
        };
        static constexpr std::size_t AbilitySinkCapacity = 4;
        std::array<AbilitySinkEntry, AbilitySinkCapacity> abilitySinks_ = {};
        std::atomic_uint routedAttackCount_{0};
        std::atomic_uint observedPlayerAttackCount_{0};
    };
}
