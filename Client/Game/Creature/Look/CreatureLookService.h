#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Look/Hooks/CreatureFacingInputRouterHook.h"

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
        ~CreatureLookService();

        bool Initialize(
            EntityService& entities,
            const core::Diagnostics& diagnostics);
        bool RouteMovementFacing(Entity* target);
        void ClearMovementFacing() noexcept;

        [[nodiscard]] unsigned int RoutedMovementFacingCount() const noexcept;

    private:
        EntityService* entities_ = nullptr;
        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        CreatureFacingInputRouterHook facingRouterHook_;
        Entity* retainedTarget_ = nullptr;
    };
}
