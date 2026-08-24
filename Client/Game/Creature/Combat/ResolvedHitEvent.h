#pragma once

#include <cstddef>
#include <cstdint>

namespace fable::game::creature::combat
{
    struct ResolvedHitEvent final
    {
        struct Vector final
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        std::uint64_t targetThingUid = 0;
        std::uint64_t sourceThingUid = 0;
        float previousHealth = -1.0f;
        float currentHealth = -1.0f;
        float maximumHealth = -1.0f;
        Vector position = {};
        Vector direction = {};
        std::uint64_t observedAt = 0;
        // Concrete victim action selected by the authoritative native OnHit
        // path. This is diagnostic/recipe data only; no process pointer is
        // retained or serialized.
        std::uint32_t reactionAnimationId = 0;
        std::uint32_t threadId = 0;
        char reactionActionType[128] = {};

        std::uint8_t positionFlag = 0;
        std::uint8_t directionFlag = 0;
        bool hasPreviousHealth = false;
        bool hasCurrentHealth = false;
        bool knockDown = false;
        bool decapitate = false;
        bool blockable = false;
        bool flourish = false;
        bool epicSpell = false;
        bool blockCounter = false;
        bool playHitResponse = false;
        bool playHitResponseOverrideSet = false;
        bool moveBack = false;
        bool createParticleEffectOnHit = false;
        bool createDustParticleEffectOnHit = false;
        bool guaranteeHit = false;
        bool blocked = false;
        bool hitNegated = false;
        bool causeRecoil = false;
    };
}
