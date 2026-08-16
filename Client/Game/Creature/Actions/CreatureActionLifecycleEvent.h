#pragma once

#include <cstdint>

namespace fable::game::creature::actions
{
    enum class CreatureActionLifecyclePhase : std::uint8_t
    {
        Submitted = 1,
        Finished = 2,
    };

    struct CreatureActionLifecycleEvent final
    {
        CreatureActionLifecyclePhase phase =
            CreatureActionLifecyclePhase::Submitted;
        std::uint64_t thingUid = 0;
        std::uint16_t mapId = 0;
        std::uint32_t animationId = 0;
        bool accepted = false;
        void* creature = nullptr;
        void* action = nullptr;
        char actionType[128] = {};
    };
}
