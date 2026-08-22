#pragma once

#include "Multiplayer/Protocol/PlayerState.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace fable::multiplayer::replication
{
    struct RemotePlayerSnapshot final
    {
        PlayerState state;
        std::uint64_t receivedAt = 0;
    };

    // Current state is keyed by actor lifecycle. This is not snapshot history:
    // every live remote player occupies one replace-in-place channel entry.
    class RemotePlayerChannels final
    {
    public:
        bool Apply(const PlayerState& update, std::uint64_t receivedAt);
        [[nodiscard]] std::vector<RemotePlayerSnapshot> Snapshots() const;
        [[nodiscard]] const PlayerState* Find(
            std::uint64_t actorId) const noexcept;
        void Remove(std::uint64_t actorId) noexcept;
        void Clear() noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;

    private:
        std::unordered_map<std::uint64_t, RemotePlayerSnapshot> channels_;
    };
}
