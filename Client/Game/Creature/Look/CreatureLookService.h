#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Look/Hooks/CreatureFacingInputRouterHook.h"

#include <vector>

namespace fable::game
{
    class Entity;
    class EntityService;
}

namespace fable::game::creature::look
{
    class CreatureLookService final
    {
    public:
        using ReplicatedMovementProvider =
            CreatureFacingInputRouterHook::ReplicatedMovementProvider;
        using CreatureFrameObserver =
            CreatureFacingInputRouterHook::FrameObserver;

        ~CreatureLookService();

        bool Initialize(
            EntityService& entities,
            const core::Diagnostics& diagnostics);
        bool RouteMovementFacing(Entity* target);
        bool RouteReplicatedMovement(
            Entity* target,
            ReplicatedMovementProvider provider,
            void* context);
        bool RouteReplicatedNativeMovement(
            void* nativeTarget,
            ReplicatedMovementProvider provider,
            void* context);
        void StopRouting(
            Entity* target,
            bool restoreAutonomousLook = true) noexcept;
        void StopRoutingNative(void* nativeTarget) noexcept;
        bool DriveReplicatedMovement(Entity* target);
        bool DriveReplicatedNativeMovement(void* nativeTarget);
        void SetCreatureFrameObserver(
            CreatureFrameObserver observer,
            void* context) noexcept;
        void ClearMovementFacing() noexcept;

        [[nodiscard]] unsigned int RoutedMovementFacingCount() const noexcept;
        [[nodiscard]] HMODULE GameModule() const noexcept;

    private:
        struct RoutedTarget final
        {
            Entity* entity = nullptr;
            void* nativeThing = nullptr;
        };

        bool Route(
            Entity* target,
            ReplicatedMovementProvider provider,
            void* context);

        EntityService* entities_ = nullptr;
        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        CreatureFacingInputRouterHook facingRouterHook_;
        std::vector<RoutedTarget> retainedTargets_;
    };
}
