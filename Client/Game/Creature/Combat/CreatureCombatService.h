#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Combat/Hooks/PlayerAttackAbilityHook.h"
#include "Game/Creature/Combat/Hooks/CombatHealthMutationHook.h"
#include "Game/Creature/Combat/PlayerAttackEvent.h"

#include <Windows.h>

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
        using PlayerAttackSink = void(*)(
            void* context,
            const PlayerAttackEvent& event);
        using HealthMutationSink = CombatHealthMutationHook::EventSink;

        ~CreatureCombatService();

        bool Initialize(
            EntityService& entities,
            const core::Diagnostics& diagnostics);
        bool RoutePlayerCombat(Entity* hero, Entity* puppet);
        bool ResolvePlayerAttackCreature(
            void* sourceCreature,
            void*& routedCreature) noexcept;
        void ObservePlayerAttack(
            void* sourceCreature,
            unsigned int abilityId,
            float charge) noexcept;
        void SetPlayerAttackSink(
            PlayerAttackSink sink,
            void* context) noexcept;
        void SetHealthMutationSink(
            HealthMutationSink sink,
            void* context) noexcept;
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
        Entity* retainedHero_ = nullptr;
        Entity* retainedPuppet_ = nullptr;
        PlayerAttackAbilityHook playerAttackAbilityHook_;
        CombatHealthMutationHook combatHealthMutationHook_;
        std::atomic<PlayerAttackSink> playerAttackSink_{nullptr};
        std::atomic<void*> playerAttackSinkContext_{nullptr};
        std::atomic_uint routedAttackCount_{0};
        std::atomic_uint observedPlayerAttackCount_{0};
    };
}
