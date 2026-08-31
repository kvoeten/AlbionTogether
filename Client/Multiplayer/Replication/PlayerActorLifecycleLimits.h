#pragma once

#include <cstddef>

namespace fable::multiplayer::replication::player_actor_lifecycle
{
    // One bounded source-of-truth admission limit shared by the canonical
    // lifecycle store and its materialized remote channels.
    inline constexpr std::size_t MaxTrackedActors = 256;
}
