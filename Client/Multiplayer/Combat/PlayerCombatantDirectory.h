#pragma once

#include <Windows.h>

#include <cstdint>
#include <unordered_map>

namespace fable::multiplayer::combat
{
    // Process-local bridge between stable multiplayer actor identities and
    // their current native Hero presentations. Native pointers never cross
    // the wire and are replaced whenever a world transition recreates them.
    class PlayerCombatantDirectory final
    {
    public:
        bool Bind(std::uint64_t actorId, void* creature) noexcept;
        void Unbind(std::uint64_t actorId, void* creature = nullptr) noexcept;
        [[nodiscard]] std::uint64_t FindActor(void* creature) const noexcept;
        [[nodiscard]] std::uint64_t FindActorByThingUid(
            std::uint64_t thingUid) const noexcept;
        [[nodiscard]] void* FindCreature(std::uint64_t actorId) const noexcept;
        void Clear() noexcept;

    private:
        mutable SRWLOCK lock_ = SRWLOCK_INIT;
        std::unordered_map<std::uint64_t, void*> creaturesByActor_;
        std::unordered_map<void*, std::uint64_t> actorsByCreature_;
        std::unordered_map<std::uint64_t, std::uint64_t> actorsByThingUid_;
    };
}
