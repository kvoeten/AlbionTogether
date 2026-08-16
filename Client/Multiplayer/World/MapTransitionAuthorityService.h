#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/World/Travel/WorldTravelPreparation.h"

#include <array>
#include <atomic>
#include <cstddef>

namespace fable::game::world::travel
{
    class WorldTravelObserver;
}

namespace fable::multiplayer::authority
{
    class AuthorityReplication;
}

namespace fable::multiplayer::world
{
    // Converts native pre-load travel observations into bounded, two-phase
    // host authority preparations. It never owns leases or saved-map data.
    class MapTransitionAuthorityService final
    {
    public:
        void Initialize(
            authority::AuthorityReplication& authority,
            const core::Diagnostics& diagnostics) noexcept;
        bool Attach(
            game::world::travel::WorldTravelObserver& observer) noexcept;
        bool Process();
        bool ConsumeSourceDeparture(std::uint16_t& sourceMapId) noexcept;
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t QueueCapacity = 4;

        static bool ObservePreparation(
            void* context,
            const game::world::travel::WorldTravelPreparation& preparation)
            noexcept;
        static void ObserveDeparture(
            void* context,
            const game::world::travel::WorldTravelPreparation& preparation)
            noexcept;
        bool Enqueue(
            const game::world::travel::WorldTravelPreparation& preparation)
            noexcept;

        authority::AuthorityReplication* authority_ = nullptr;
        game::world::travel::WorldTravelObserver* observer_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::array<
            game::world::travel::WorldTravelPreparation,
            QueueCapacity> queue_ = {};
        std::atomic_size_t writeIndex_{0};
        std::atomic_size_t readIndex_{0};
        std::atomic_uint droppedCount_{0};
        std::atomic_uint sourceDepartureMapId_{0};
    };
}
