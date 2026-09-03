#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Math/Vector3.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace fable::game
{
    class EntityService;
}

namespace fable::multiplayer
{
    class MultiplayerRuntimeGraph;
}

namespace fable::automation::local_instance
{
    // Test-only local driver for the adult-town fixture. It asks the Hero's
    // real physics navigator to walk backwards into the adjacent level, so
    // transition acceptance does not steal Windows or DirectInput focus.
    class MapTransitionAcceptanceDriver final
    {
    public:
        void Initialize(
            bool enabled,
            bool returnToSource,
            game::EntityService& entities,
            ::fable::multiplayer::MultiplayerRuntimeGraph& multiplayer,
            const core::Diagnostics& diagnostics) noexcept;
        void Tick(bool remotePresentationReady) noexcept;
        bool ProcessGameThreadIdle() noexcept;
        void Shutdown() noexcept;

    private:
        game::EntityService* entities_ = nullptr;
        ::fable::multiplayer::MultiplayerRuntimeGraph* multiplayer_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::string sourceMap_;
        std::string destinationMap_;
        game::Vector3 travelPosition_ = {};
        float travelFacing_ = 0.0f;
        std::uint64_t phaseStartedAt_ = 0;
        std::uint64_t nextTravelAttemptAt_ = 0;
        unsigned int requestCount_ = 0;
        unsigned int outboundRequestCount_ = 0;
        unsigned int travelInvocationCount_ = 0;
        std::uint16_t sourceMapId_ = 0;
        std::uint16_t destinationMapId_ = 0;
        std::uint16_t requestedDestinationMapId_ = 0;
        std::atomic_bool travelQueued_{false};
        bool routeRequested_ = false;
        bool returnToSource_ = false;
        bool enabled_ = false;
        bool completed_ = false;
    };
}
