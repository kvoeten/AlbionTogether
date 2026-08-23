#pragma once

#include "Game/Creature/Actions/CreatureActionLifecycleEvent.h"
#include "Game/Creature/Combat/CreatureAbilityEvent.h"
#include "Game/HeroPawn/Abilities/HeroAbilityEvent.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace fable::multiplayer::replication
{
    // Bridges native callbacks to the game-window processing path. Native
    // callbacks may arrive on arbitrary game threads, so this owns the
    // synchronization and the bounded ingress queues instead of exposing
    // those details through PlayerActionReplication.
    class PlayerActionEventQueue final
    {
    public:
        static constexpr std::size_t Capacity = 1024;

        struct Batch final
        {
            std::deque<game::creature::combat::CreatureAbilityEvent>
                abilities;
            std::deque<game::creature::actions::CreatureActionLifecycleEvent>
                actions;
            std::deque<game::hero_pawn::abilities::HeroAbilityEvent>
                heroAbilities;
        };

        void SetAccepting(bool accepting) noexcept;
        void Drain(Batch& batch) noexcept;
        void Clear() noexcept;

        void Enqueue(
            const game::creature::combat::CreatureAbilityEvent& event)
            noexcept;
        void Enqueue(
            const game::creature::actions::CreatureActionLifecycleEvent& event)
            noexcept;
        void Enqueue(
            const game::hero_pawn::abilities::HeroAbilityEvent& event)
            noexcept;

    private:
        static bool IsWeaponTransitionAction(const char* actionType) noexcept;
        void CountDrop() noexcept;

        std::mutex mutex_;
        std::deque<game::creature::combat::CreatureAbilityEvent> abilities_;
        std::deque<game::creature::actions::CreatureActionLifecycleEvent>
            actions_;
        std::deque<game::hero_pawn::abilities::HeroAbilityEvent>
            heroAbilities_;
        std::atomic_bool accepting_{false};
        std::atomic_uint dropped_{0};
    };
}
