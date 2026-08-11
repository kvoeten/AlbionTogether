#pragma once

#include "Game/Math/Vector3.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionService;

    class CreatureLocomotionState final
    {
    public:
        void AddRef() noexcept;
        void Release() noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool HasPhysicsNavigator() const noexcept;
        [[nodiscard]] bool HasCreatureNavigation() const noexcept;
        [[nodiscard]] bool HasAnimationComplex() const noexcept;
        [[nodiscard]] bool HasCachedNavigationSolution() const noexcept;
        [[nodiscard]] unsigned int ComponentCount() const noexcept;
        [[nodiscard]] unsigned int AnimationStateHash() const noexcept;
        [[nodiscard]] Vector3 PhysicsPosition() const noexcept;

    private:
        friend class CreatureLocomotionService;

        CreatureLocomotionState() = default;
        ~CreatureLocomotionState() = default;

        std::atomic_uint referenceCount_{1};
        bool valid_ = false;
        bool hasPhysicsNavigator_ = false;
        bool hasCreatureNavigation_ = false;
        bool hasAnimationComplex_ = false;
        bool navigationSolutionCached_ = false;
        std::size_t componentCount_ = 0;
        std::uint32_t animationStateHash_ = 0;
        Vector3 physicsPosition_ = {};
    };
}
