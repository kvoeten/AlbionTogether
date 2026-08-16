#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Animation/Native/AnimationPlaybackFunctions.h"

#include <atomic>
#include <cstdint>

namespace fable::game
{
    class EntityService;
}

namespace fable::game::creature::animation
{
    class CreatureAnimationService final
    {
    public:
        bool Initialize(
            EntityService& entities,
            const core::Diagnostics& diagnostics);
        bool PlayAuthoritative(
            void* creature,
            std::uint32_t animationId,
            std::uint32_t flags = 0) noexcept;
        [[nodiscard]] bool IsReady() const noexcept;

    private:
        static constexpr unsigned int DiagnosticEventLimit = 256;

        native::AnimationPlaybackFunctions functions_;
        core::Diagnostics diagnostics_ = {};
        std::atomic_uint playbackCount_{0};
    };
}
