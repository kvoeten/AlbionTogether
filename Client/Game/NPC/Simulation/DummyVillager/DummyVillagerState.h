#pragma once

#include <cstdint>

namespace fable::game::npc::simulation
{
    struct DummyVillagerState final
    {
        // The durable bridge between the low-simulation villager and its
        // high-simulation creature. This is identity metadata, not mutable
        // replicated schedule state, so authoritative schedule application
        // never overwrites it.
        std::uint64_t creatureUid = 0;
        std::int32_t recreationDay = 0;
        std::int32_t recreationFrame = 0;
        bool respawnable = false;
        bool guard = false;
        bool componentPresent = false;
    };

    inline bool operator==(
        const DummyVillagerState& left,
        const DummyVillagerState& right) noexcept
    {
        return left.recreationDay == right.recreationDay &&
            left.recreationFrame == right.recreationFrame &&
            left.respawnable == right.respawnable &&
            left.guard == right.guard &&
            left.componentPresent == right.componentPresent;
    }

    inline bool operator!=(
        const DummyVillagerState& left,
        const DummyVillagerState& right) noexcept
    {
        return !(left == right);
    }
}
