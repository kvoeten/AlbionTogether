#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Animation/Hooks/CreatureActionAnimationSelectionHook.h"
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
        ~CreatureAnimationService();

        bool Initialize(
            EntityService& entities,
            const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        bool PlayAuthoritative(
            void* creature,
            std::uint32_t animationId,
            std::uint32_t flags = 0) noexcept;
        bool BeginReplicatedActionSelection(
            void* creature,
            const char* actionType,
            std::uint32_t animationId) noexcept;
        void EndReplicatedActionSelection() noexcept;
        bool AttachActionLifecycleObserver(
            actions::CreatureActionLifecycleObserver& observer) noexcept;
        void DetachActionLifecycleObserver() noexcept;
        [[nodiscard]] bool IsReady() const noexcept;

    private:
        static constexpr unsigned int DiagnosticEventLimit = 256;

        native::AnimationPlaybackFunctions functions_;
        CreatureActionAnimationSelectionHook actionSelectionHook_;
        core::Diagnostics diagnostics_ = {};
        std::atomic_uint playbackCount_{0};
    };
}
