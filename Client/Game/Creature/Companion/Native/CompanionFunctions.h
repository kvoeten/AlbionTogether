#pragma once

#include <Windows.h>

#include <cstdint>

namespace fable::game::creature::companion::native
{
    struct CompanionRegistration final
    {
        void* followerEnemy = nullptr;
        void* heroRegionFollower = nullptr;
        std::uint32_t followerCountBefore = 0;
        std::uint32_t followerCountAfter = 0;
        bool factionAssigned = false;
        bool heroAddedAsAlly = false;
        bool followerRegistered = false;
        bool alreadyRegistered = false;
    };

    // Native access to the same faction, ally, and CTCRegionFollower path used
    // by Fable's hireable and escorted creatures. Membership deliberately
    // survives map-local presentation dormancy so the retail party UI can
    // represent a follower in another region. Replicated movement remains the
    // live proxy's locomotion owner; this does not enqueue a follow command.
    class CompanionFunctions final
    {
    public:
        [[nodiscard]] static bool RegisterWithHero(
            HMODULE gameModule,
            void* followerThing,
            void* heroThing,
            CompanionRegistration& registration) noexcept;

        [[nodiscard]] static bool UnregisterFromHero(
            HMODULE gameModule,
            void* followerThing,
            void* heroThing) noexcept;
    };
}
