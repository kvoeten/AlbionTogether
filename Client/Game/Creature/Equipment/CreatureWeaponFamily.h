#pragma once

#include <cstdint>

namespace fable::game::creature::equipment
{
    // The AI carrying stack materializes one active weapon family at a time.
    // Both definition IDs remain in the replicated loadout; this value only
    // selects which one is currently attached to the creature.
    enum class CreatureWeaponFamily : std::uint8_t
    {
        None = 0,
        Melee = 1,
        Ranged = 2,
    };
}
