#include "CreatureLocomotionState.h"

namespace fable::game::creature::locomotion
{
    void CreatureLocomotionState::AddRef() noexcept
    {
        referenceCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void CreatureLocomotionState::Release() noexcept
    {
        if (referenceCount_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            delete this;
        }
    }

    bool CreatureLocomotionState::IsValid() const noexcept
    {
        return valid_;
    }

    bool CreatureLocomotionState::HasPhysicsNavigator() const noexcept
    {
        return hasPhysicsNavigator_;
    }

    bool CreatureLocomotionState::HasCreatureNavigation() const noexcept
    {
        return hasCreatureNavigation_;
    }

    bool CreatureLocomotionState::HasAnimationComplex() const noexcept
    {
        return hasAnimationComplex_;
    }

    bool CreatureLocomotionState::HasCachedNavigationSolution() const noexcept
    {
        return navigationSolutionCached_;
    }

    unsigned int CreatureLocomotionState::ComponentCount() const noexcept
    {
        return static_cast<unsigned int>(componentCount_);
    }

    unsigned int CreatureLocomotionState::AnimationStateHash() const noexcept
    {
        return animationStateHash_;
    }

    Vector3 CreatureLocomotionState::PhysicsPosition() const noexcept
    {
        return physicsPosition_;
    }
}
