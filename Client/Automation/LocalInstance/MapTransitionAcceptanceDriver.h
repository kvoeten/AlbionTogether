#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstdint>
#include <string>

namespace fable::game
{
    class EntityService;
}

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionService;
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
            game::EntityService& entities,
            game::creature::locomotion::CreatureLocomotionService& locomotion,
            const core::Diagnostics& diagnostics) noexcept;
        void Tick(float deltaSeconds, bool remotePresentationReady);
        void Shutdown() noexcept;

    private:
        game::EntityService* entities_ = nullptr;
        game::creature::locomotion::CreatureLocomotionService* locomotion_ =
            nullptr;
        core::Diagnostics diagnostics_ = {};
        std::string sourceMap_;
        std::uint64_t startedAt_ = 0;
        unsigned int requestCount_ = 0;
        bool enabled_ = false;
        bool completed_ = false;
    };
}
