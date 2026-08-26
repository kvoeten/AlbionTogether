#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstdint>
#include <string>

namespace fable::game
{
    class EntityService;
}

namespace fable::game::creature::animation
{
    class CreatureAnimationService;
}

namespace fable::game::hero_pawn::expression
{
    class RemoteHeroExpressionController final
    {
    public:
        void Initialize(
            game::EntityService& entities,
            game::creature::animation::CreatureAnimationService& animation,
            const core::Diagnostics& diagnostics) noexcept;
        bool Perform(
            void* performer,
            void* target,
            const std::string& expressionDefinition,
            const std::string& resolvedActionType,
            std::uint32_t resolvedAnimationId,
            std::int32_t durationTicks,
            std::int32_t triggerTicks);
        void Shutdown() noexcept;

    private:
        game::EntityService* entities_ = nullptr;
        game::creature::animation::CreatureAnimationService* animation_ =
            nullptr;
        core::Diagnostics diagnostics_ = {};
    };
}
