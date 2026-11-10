#pragma once

#include "Multiplayer/Presentation/RemoteHero/RemoteHeroActor.h"

namespace fable::multiplayer::presentation::remote_hero
{
    // Private implementation helper for the native lifetime of one remote
    // Hero. RemoteHeroActor remains the public actor aggregate; appearance,
    // equipment, combat, ability, and movement controllers stay owned by it.
    class RemoteHeroNativeLifecycle final
    {
    public:
        explicit RemoteHeroNativeLifecycle(RemoteHeroActor& owner) noexcept
            : owner_(owner)
        {
        }

        bool Spawn(const PlayerState& state);
        RemoteHeroActivationResult Activate(
            const PlayerState& state,
            const std::string& localMap,
            game::Entity* localHero,
            std::uint64_t receivedAt);
        void DriveMovement();
        void Retire() noexcept;

    private:
        void ApplyActivePresentationFlags(bool configureUnloadPolicy) noexcept;
        RemoteHeroActor& owner_;
    };
}

