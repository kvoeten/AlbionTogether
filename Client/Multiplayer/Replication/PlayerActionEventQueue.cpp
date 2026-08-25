#include "PlayerActionEventQueue.h"
#include "PlayerActionSemantics.h"

namespace fable::multiplayer::replication
{
    void PlayerActionEventQueue::SetAccepting(bool accepting) noexcept
    {
        accepting_.store(accepting, std::memory_order_release);
    }

    void PlayerActionEventQueue::Drain(Batch& batch) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        batch.abilities.swap(abilities_);
        batch.actions.swap(actions_);
        batch.heroAbilities.swap(heroAbilities_);
        batch.modeSources.swap(modeSources_);
    }

    void PlayerActionEventQueue::Clear() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        abilities_.clear();
        actions_.clear();
        heroAbilities_.clear();
        modeSources_.clear();
        dropped_.store(0, std::memory_order_release);
    }

    void PlayerActionEventQueue::CountDrop() noexcept
    {
        dropped_.fetch_add(1, std::memory_order_acq_rel);
    }

    void PlayerActionEventQueue::Enqueue(
        const game::creature::combat::CreatureAbilityEvent& event) noexcept
    {
        if (!accepting_.load(std::memory_order_acquire))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_.load(std::memory_order_relaxed))
        {
            return;
        }
        if (abilities_.size() >= Capacity)
        {
            CountDrop();
            return;
        }
        abilities_.push_back(event);
    }

    void PlayerActionEventQueue::Enqueue(
        const game::creature::actions::CreatureActionLifecycleEvent& event)
        noexcept
    {
        if (!accepting_.load(std::memory_order_acquire) ||
            event.phase != game::creature::actions::
                CreatureActionLifecyclePhase::Submitted ||
            !player_action_semantics::IsReplicatedAction(event.actionType) ||
            (player_action_semantics::IsRangedAimStart(event.actionType) &&
                !event.accepted))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_.load(std::memory_order_relaxed))
        {
            return;
        }
        if (actions_.size() >= Capacity)
        {
            CountDrop();
            return;
        }
        actions_.push_back(event);
    }

    void PlayerActionEventQueue::Enqueue(
        const game::hero_pawn::abilities::HeroAbilityEvent& event) noexcept
    {
        if (!accepting_.load(std::memory_order_acquire))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_.load(std::memory_order_relaxed))
        {
            return;
        }
        if (heroAbilities_.size() >= Capacity)
        {
            CountDrop();
            return;
        }
        heroAbilities_.push_back(event);
    }

    void PlayerActionEventQueue::Enqueue(
        const game::creature::locomotion::CreatureModeSourceEvent& event)
        noexcept
    {
        if (!accepting_.load(std::memory_order_acquire) ||
            event.owner == nullptr || event.source != 25 || event.added ||
            !event.changed)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_.load(std::memory_order_relaxed))
        {
            return;
        }
        if (modeSources_.size() >= Capacity)
        {
            CountDrop();
            return;
        }
        modeSources_.push_back(event);
    }
}
