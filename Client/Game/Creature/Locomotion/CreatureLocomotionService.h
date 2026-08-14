#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Locomotion/Hooks/CreatureFrameInputRouterHook.h"
#include "Game/Creature/Locomotion/Hooks/PhysicsMovementInputHook.h"
#include "Game/Math/Vector3.h"

#include <Windows.h>

namespace fable::game
{
    class Entity;
    class EntityService;
}

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionState;

    class CreatureLocomotionService final
    {
    public:
        using PlayerFrameObserver = CreatureFrameInputRouterHook::FrameObserver;

        ~CreatureLocomotionService();

        bool Initialize(
            EntityService& entities,
            const core::Diagnostics& diagnostics);

        [[nodiscard]] CreatureLocomotionState* Inspect(Entity* entity) const;
        bool MirrorPhysicsWorldPosition(Entity* source, Entity* target);
        void ClearPhysicsWorldPositionMirror() noexcept;
        bool MirrorAnimationMotion(Entity* source, Entity* target);
        void ClearAnimationMotionMirror() noexcept;
        bool RoutePlayerFrameInput(Entity* source, Entity* target);
        void ClearPlayerFrameInputRouter() noexcept;
        void SetPlayerFrameObserver(
            PlayerFrameObserver observer,
            void* context) noexcept;
        bool RouteHeroShadow(Entity* sourcePuppet, Entity* targetHero);
        void ClearHeroShadow() noexcept;
        void TickHeroShadow();
        bool RequestPosition(
            Entity* entity,
            const Vector3& desiredPosition) const;
        bool SetPhysicsWorldPosition(Entity* entity, const Vector3& worldPosition) const;
        [[nodiscard]] unsigned int MirroredPhysicsWorldPositionCount() const noexcept;
        [[nodiscard]] unsigned int MirroredAnimationMotionCount() const noexcept;
        [[nodiscard]] unsigned int RoutedPlayerFrameCount() const noexcept;
        [[nodiscard]] unsigned int HeroShadowUpdateCount() const noexcept;

    private:
        EntityService* entities_ = nullptr;
        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        CreatureFrameInputRouterHook frameInputRouterHook_;
        PhysicsWorldPositionMirrorHook worldPositionMirrorHook_;
        Entity* retainedShadowSource_ = nullptr;
        Entity* retainedShadowTarget_ = nullptr;
        void* shadowSourceNavigator_ = nullptr;
        void* shadowTargetControlled_ = nullptr;
        unsigned int heroShadowUpdateCount_ = 0;
    };
}
