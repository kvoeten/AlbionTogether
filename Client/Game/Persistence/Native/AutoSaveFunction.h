#pragma once

#include <cstdint>

namespace fable::game
{
    class EntityService;
}

namespace fable::game::persistence::native
{
    struct AutoSaveState final
    {
        std::int32_t saveState = 0;
        std::int32_t loadState = 0;
    };

    // Saves to the current profile's AutoSave path through the same complete
    // native boundary used by the in-game menu. Fable remains the sole owner
    // of save format, Hero data, file writing, and error handling.
    class AutoSaveFunction final
    {
    public:
        static bool Invoke(EntityService& entities) noexcept;
        static bool ReadState(
            EntityService& entities,
            AutoSaveState& state) noexcept;
    };
}
