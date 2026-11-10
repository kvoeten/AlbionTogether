#include "EntityActionCaptureQueue.h"

#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Combat/CreatureCombatService.h"

namespace fable::multiplayer::replication::entity_actions
{
    bool EntityActionCaptureQueue::Initialize(
        game::creature::combat::CreatureCombatService& combat)
    {
        Shutdown();
        combat_ = &combat;
        accepting_.store(true, std::memory_order_release);
        if (!combat_->AddAbilitySink(
                &EntityActionCaptureQueue::CaptureAbility, this))
        {
            Shutdown();
            return false;
        }
        return true;
    }

    bool EntityActionCaptureQueue::Attach(
        game::creature::actions::CreatureActionLifecycleObserver& observer)
    {
        if (!accepting_.load(std::memory_order_acquire) ||
            !observer.IsInstalled())
        {
            return false;
        }
        if (observer_ != nullptr && observer_ != &observer)
        {
            observer_->RemoveEventSink(
                &EntityActionCaptureQueue::CaptureAction, this);
        }
        observer_ = &observer;
        if (!observer_->AddEventSink(
                &EntityActionCaptureQueue::CaptureAction, this))
        {
            observer_ = nullptr;
            return false;
        }
        return true;
    }

    void EntityActionCaptureQueue::Drain(
        std::deque<game::creature::actions::CreatureActionLifecycleEvent>&
            actions,
        std::deque<game::creature::combat::CreatureAbilityEvent>& abilities)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        actions.swap(actions_);
        abilities.swap(abilities_);
    }

    unsigned int EntityActionCaptureQueue::DroppedCount() const noexcept
    {
        return dropped_.load(std::memory_order_acquire);
    }

    void EntityActionCaptureQueue::CaptureAction(
        void* const context,
        const game::creature::actions::CreatureActionLifecycleEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<EntityActionCaptureQueue*>(context)->Enqueue(event);
        }
    }

    void EntityActionCaptureQueue::CaptureAbility(
        void* const context,
        const game::creature::combat::CreatureAbilityEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<EntityActionCaptureQueue*>(context)->Enqueue(event);
        }
    }

    void EntityActionCaptureQueue::Enqueue(
        const game::creature::actions::CreatureActionLifecycleEvent& event)
        noexcept
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
        if (actions_.size() >= Capacity)
        {
            dropped_.fetch_add(1, std::memory_order_acq_rel);
            return;
        }
        actions_.push_back(event);
    }

    void EntityActionCaptureQueue::Enqueue(
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
            dropped_.fetch_add(1, std::memory_order_acq_rel);
            return;
        }
        abilities_.push_back(event);
    }

    void EntityActionCaptureQueue::Shutdown() noexcept
    {
        accepting_.store(false, std::memory_order_release);
        if (combat_ != nullptr)
        {
            combat_->RemoveAbilitySink(
                &EntityActionCaptureQueue::CaptureAbility, this);
        }
        if (observer_ != nullptr)
        {
            observer_->RemoveEventSink(
                &EntityActionCaptureQueue::CaptureAction, this);
        }
        observer_ = nullptr;
        combat_ = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            actions_.clear();
            abilities_.clear();
        }
        dropped_.store(0, std::memory_order_release);
    }
}
