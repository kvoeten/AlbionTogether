#pragma once

#include <cstdint>

namespace fable::game::creature::locomotion
{
    // Pointer identity is consumed synchronously by the local replication
    // boundary only. Nothing from this native observation is serialized.
    struct CreatureModeSourceEvent final
    {
        void* owner = nullptr;
        int source = 0;
        std::uint64_t observedAt = 0;
        bool added = false;
        bool changed = false;
    };
}
