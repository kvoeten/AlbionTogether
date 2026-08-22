#pragma once

#include "Game/Entity/Presence/ThingPresenceEvent.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fable::multiplayer::entities
{
    struct LiveEntityRecord final
    {
        std::uint64_t thingUid = 0;
        std::uint64_t villageUid = 0;
        std::uint32_t localIncarnation = 0;
        std::uint16_t mapId = 0;
        std::uint16_t definitionIndex = 0;
        std::string scriptName;
        game::Vector3 position = {};
        float facing = 0.0f;
        bool hasTransform = false;
        bool gamePersistent = false;
        bool levelPersistent = false;
        bool creature = false;
        bool hasHeroMorph = false;
        bool hasVillageMembership = false;
        bool summonedCreature = false;
        bool abilityOwnedTransient = false;
        void* thing = nullptr;
        void* mapwhoComponent = nullptr;
    };

    enum class LiveEntityChangeKind : std::uint8_t
    {
        None = 0,
        Registered = 1,
        Rebound = 2,
        Unregistered = 3,
    };

    struct LiveEntityChange final
    {
        LiveEntityChangeKind kind = LiveEntityChangeKind::None;
        LiveEntityRecord record = {};
    };

    // Process-local current incarnations only. Unregistration erases a record;
    // the monotonically assigned local incarnation prevents stale native
    // callbacks from matching a later instance without retaining tombstones.
    class LiveEntityRegistry final
    {
    public:
        bool Apply(
            const game::entity::presence::ThingPresenceEvent& event,
            LiveEntityChange& change);
        [[nodiscard]] const LiveEntityRecord* Find(
            std::uint64_t thingUid) const noexcept;
        bool Remap(
            std::uint64_t localUid,
            std::uint64_t canonicalUid) noexcept;
        [[nodiscard]] std::vector<LiveEntityRecord> Snapshot() const;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] static bool IsReplicable(
            const LiveEntityRecord& record) noexcept;
        [[nodiscard]] static bool IsPlayerPresentation(
            const LiveEntityRecord& record) noexcept;
        void Clear() noexcept;

    private:
        std::uint32_t NextIncarnation() noexcept;

        std::unordered_map<std::uint64_t, LiveEntityRecord> records_;
        std::uint32_t nextIncarnation_ = 0;
    };
}
