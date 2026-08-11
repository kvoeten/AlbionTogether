#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Combat/Hooks/PlayerAttackAbilityHook.h"

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
        ~CreatureCombatService();

        bool Initialize(
            EntityService& entities,
            const core::Diagnostics& diagnostics);
        bool RoutePlayerCombat(Entity* hero, Entity* puppet);
        bool ResolvePlayerAttackCreature(
            void* sourceCreature,
            void*& routedCreature) noexcept;
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
        std::atomic_uint routedAttackCount_{0};
    };
}
