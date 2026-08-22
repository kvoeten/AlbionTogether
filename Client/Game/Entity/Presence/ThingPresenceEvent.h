#pragma once

#include "Game/Math/Vector3.h"

#include <cstdint>
#include <array>

namespace fable::game::entity::presence
{
    enum class ThingPresencePhase : std::uint8_t
    {
        Registered = 1,
        Unregistered = 2,
    };

    // A synchronous view of a CThing crossing the live CTCMapwho boundary.
    // Native pointers are process-local diagnostics only; UID and map ID are
    // the fields that may be promoted into multiplayer identity.
    struct ThingPresenceEvent final
    {
        ThingPresencePhase phase = ThingPresencePhase::Registered;
        std::uint64_t thingUid = 0;
        std::uint64_t villageUid = 0;
        std::uint16_t mapId = 0;
        std::uint16_t definitionIndex = 0;
        std::array<char, 96> scriptName = {};
        game::Vector3 position = {};
        float facing = 0.0f;
        bool hasTransform = false;
        bool gamePersistent = false;
        bool levelPersistent = false;
        bool creature = false;
        // CTCHeroMorph is not exclusive to the selected-save Hero. Retail
        // adult NPCs can carry it as part of their appearance stack too.
        bool hasHeroMorph = false;
        bool hasVillageMembership = false;
        // Native CTCSummonedCreature live-state marker. This is available at
        // registration time, before the Summon action assigns a script name.
        bool summonedCreature = false;
        // The entity was created inside a native action that owns its own
        // replicated lifecycle (for example Summon or Ghost Sword).
        bool abilityOwnedTransient = false;
        // True only for the terminal CTCMapwho owner destruction path. A
        // normal unregister can be followed by registration of the same
        // native Thing and must not discard its canonical network alias.
        bool destroyed = false;
        void* thing = nullptr;
        void* component = nullptr;
    };
}
