#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Math/Vector3.h"
#include "Game/World/Travel/Native/RegionExitFunctions.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace fable::game { class EntityService; }
namespace fable::multiplayer { class MultiplayerRuntimeGraph; }

namespace fable::automation::multiplayer::transition
{
    // Deterministic retail script teleports to real connected region
    // entrances. Each peer chooses independently, so repeated runs exercise
    // split maps, crossings, and reunions without moving an actor or
    // synthesizing input.
    class MapStressAcceptanceDriver final
    {
    public:
        void Initialize(
            bool enabled,
            bool host,
            std::uint32_t seed,
            unsigned int transitionCount,
            game::EntityService& entities,
            ::fable::multiplayer::MultiplayerRuntimeGraph& multiplayer,
            const core::Diagnostics& diagnostics) noexcept;
        void Tick() noexcept;
        bool ProcessGameThreadIdle() noexcept;
        void Shutdown() noexcept;

    private:
        struct PeerMap final
        {
            std::string name;
            std::uint16_t id = 0;
            std::uint32_t epoch = 0;
        };

        [[nodiscard]] bool ReadStableMaps(
            PeerMap& local,
            PeerMap& remote) const;
        [[nodiscard]] std::vector<
            game::world::travel::native::RegionExitDescriptor>
                AvailableDestinations(const PeerMap& local);
        [[nodiscard]] bool BeginTransition(
            const PeerMap& local,
            std::uint64_t now);
        [[nodiscard]] bool RequestTravel(
            const game::world::travel::native::RegionExitDescriptor& exit)
            noexcept;
        [[nodiscard]] bool TransitionSettled(
            const PeerMap& local,
            const PeerMap& remote) const noexcept;
        void CompleteTransition(
            const PeerMap& local,
            const PeerMap& remote,
            std::uint64_t now) noexcept;
        void Fail(const char* reason) noexcept;
        void ReportStarted(const PeerMap& local) noexcept;
        void ReportRequest(
            const game::world::travel::native::RegionExitDescriptor& local)
            noexcept;
        void ReportRouteUnavailable(
            const PeerMap& local,
            std::size_t liveRecords,
            std::size_t regionExits) noexcept;
        void ReportTravelRequestFailure(const char* reason) noexcept;
        [[nodiscard]] std::size_t SharedChoice(
            std::size_t count,
            std::uint32_t salt) const noexcept;

        game::EntityService* entities_ = nullptr;
        ::fable::multiplayer::MultiplayerRuntimeGraph* multiplayer_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::uint32_t seed_ = 0;
        unsigned int transitionLimit_ = 0;
        unsigned int transitionOrdinal_ = 0;
        std::uint16_t sourceMapId_ = 0;
        std::uint32_t sourceMapEpoch_ = 0;
        std::uint16_t localDestinationMapId_ = 0;
        game::Vector3 destinationPosition_ = {};
        float destinationFacing_ = 0.0f;
        std::uint64_t phaseReadyAt_ = 0;
        std::uint64_t transitionRequestedAt_ = 0;
        std::uint64_t settledAt_ = 0;
        std::uint64_t nextTravelAttemptAt_ = 0;
        unsigned int travelInvocations_ = 0;
        std::atomic_bool travelQueued_{false};
        bool host_ = false;
        bool enabled_ = false;
        bool started_ = false;
        bool completed_ = false;
        bool failed_ = false;
        bool routeDiagnosticReported_ = false;
    };
}
