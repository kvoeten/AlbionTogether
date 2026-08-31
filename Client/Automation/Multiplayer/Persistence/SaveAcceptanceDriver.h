#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace fable::game
{
    class EntityService;
    class PlayerService;
}

namespace fable::multiplayer
{
    class MultiplayerRuntimeGraph;
}

namespace fable::automation::multiplayer::persistence
{
    // Requests one native AutoSave only after the local world and a remote
    // presentation are stable. The launcher owns file completion and reload
    // verification, keeping filesystem polling outside the injected client.
    class SaveAcceptanceDriver final
    {
    public:
        void Initialize(
            bool enabled,
            bool host,
            game::EntityService& entities,
            game::PlayerService& players,
            ::fable::multiplayer::MultiplayerRuntimeGraph& multiplayer,
            const core::Diagnostics& diagnostics) noexcept;
        void Tick(bool remotePresentationReady) noexcept;
        bool ProcessGameThreadIdle() noexcept;
        void Shutdown() noexcept;

    private:
        static constexpr std::uint64_t StableWorldMilliseconds = 3'000;
        static constexpr std::uint64_t GuestSaveDelayMilliseconds = 12'000;
        static constexpr std::uint64_t RetryMilliseconds = 1'000;
        static constexpr unsigned int MaximumAttempts = 30;

        game::EntityService* entities_ = nullptr;
        game::PlayerService* players_ = nullptr;
        ::fable::multiplayer::MultiplayerRuntimeGraph* multiplayer_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::uint64_t readyAt_ = 0;
        std::atomic_bool saveQueued_{false};
        unsigned int attempts_ = 0;
        std::int32_t lastSaveState_ = -1;
        std::int32_t lastLoadState_ = -1;
        bool host_ = false;
        bool enabled_ = false;
        bool multiplayerWorldObserved_ = false;
        bool invoked_ = false;
        bool heroMutated_ = false;
        bool failed_ = false;
    };
}
