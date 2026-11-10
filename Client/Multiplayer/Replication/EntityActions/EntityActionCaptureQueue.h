#pragma once

#include "Game/Creature/Actions/CreatureActionLifecycleEvent.h"
#include "Game/Creature/Combat/CreatureAbilityEvent.h"

#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>

namespace fable::game::creature::actions
{
    class CreatureActionLifecycleObserver;
}

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::multiplayer::replication::entity_actions
{
    // Thread-safe ingress from native Fable action callbacks. Native pointers
    // are copied only as opaque lifecycle identities so a later Finish event
    // can close the same action; multiplayer code never dereferences or
    // serializes them. Authority and network work happen on the gameplay
    // thread, and fencing clears identities whose native lifecycle vanished.
    class EntityActionCaptureQueue final
    {
    public:
        [[nodiscard]] bool Initialize(
            game::creature::combat::CreatureCombatService& combat);
        [[nodiscard]] bool Attach(
            game::creature::actions::CreatureActionLifecycleObserver&
                observer);
        void Drain(
            std::deque<game::creature::actions::
                CreatureActionLifecycleEvent>& actions,
            std::deque<game::creature::combat::CreatureAbilityEvent>&
                abilities);
        [[nodiscard]] unsigned int DroppedCount() const noexcept;
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t Capacity = 8192;

        static void CaptureAction(
            void* context,
            const game::creature::actions::CreatureActionLifecycleEvent&
                event);
        static void CaptureAbility(
            void* context,
            const game::creature::combat::CreatureAbilityEvent& event);
        void Enqueue(
            const game::creature::actions::CreatureActionLifecycleEvent&
                event) noexcept;
        void Enqueue(
            const game::creature::combat::CreatureAbilityEvent& event)
            noexcept;

        game::creature::actions::CreatureActionLifecycleObserver* observer_ =
            nullptr;
        game::creature::combat::CreatureCombatService* combat_ = nullptr;
        mutable std::mutex mutex_;
        std::deque<game::creature::actions::CreatureActionLifecycleEvent>
            actions_;
        std::deque<game::creature::combat::CreatureAbilityEvent> abilities_;
        std::atomic_bool accepting_{false};
        std::atomic_uint dropped_{0};
    };
}
