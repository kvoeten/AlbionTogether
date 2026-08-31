#pragma once

#include <Windows.h>

#include <cstdint>

namespace fable::game::world::travel::native
{
    struct RegionExitDescriptor final
    {
        std::uint64_t exitUid = 0;
        std::uint64_t destinationEntranceUid = 0;
        std::uint16_t sourceMapId = 0;
        std::uint16_t destinationMapId = 0;
    };

    // Read-only discovery of the current map's connected CTCDRegionExit
    // components. The stress driver then requests the exit's scripted-use
    // action, leaving transition scheduling and teardown with Fable.
    struct RegionExitFunctions final
    {
        [[nodiscard]] static bool Describe(
            void* nativeThing,
            std::uint64_t exitUid,
            HMODULE gameModule,
            RegionExitDescriptor& descriptor) noexcept;
    };
}
