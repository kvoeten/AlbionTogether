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
        std::uint64_t observedAt = 0;
        std::uint16_t mapId = 0;
        std::uint32_t animationId = 0;
        std::int32_t expressionDurationTicks = 0;
        std::int32_t expressionTriggerTicks = 0;
        std::uint32_t threadId = 0;
        bool accepted = false;
        void* creature = nullptr;
        void* action = nullptr;
        void* targetCreature = nullptr;
        std::uint64_t targetThingUid = 0;
        char actionType[128] = {};
        char expressionName[128] = {};
    };
}
