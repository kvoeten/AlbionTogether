#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Runtime/MultiplayerSession.h"

#include <cstdint>

namespace fable::game
{
    class Entity;
    class EntityService;
    class NpcService;
}

namespace fable::automation::multiplayer::transition
{
    // Test-only stimulus for the production owned-entity transfer seam. It
    // tears down the source incarnation normally; canonical transfer, fencing,
    // low-sim retention, and destination materialization remain the ordinary
    // multiplayer implementation under test.
    class NpcTransferAcceptanceDriver final
    {
    public:
        void Initialize(
            bool enabled,
            game::EntityService& entities,
            game::NpcService& npcs,
            ::fable::multiplayer::MultiplayerSession& multiplayer,
            const core::Diagnostics& diagnostics) noexcept;
        void Tick(bool remotePresentationReady);
        void Shutdown() noexcept;

    private:
        bool BeginSourceTeardown();

        game::EntityService* entities_ = nullptr;
        game::NpcService* npcs_ = nullptr;
        ::fable::multiplayer::MultiplayerSession* multiplayer_ = nullptr;
        game::Entity* target_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::uint64_t armedAt_ = 0;
        std::uint64_t spawnedAt_ = 0;
        std::uint64_t targetUid_ = 0;
        bool scriptRetained_ = false;
        bool enabled_ = false;
        bool completed_ = false;
    };
}
