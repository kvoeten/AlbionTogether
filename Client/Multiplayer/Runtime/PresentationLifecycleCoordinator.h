#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fable::multiplayer
{
    class MultiplayerRuntimeGraph;

    // Owns the ordered per-frame world/presentation phases.  The session
    // remains the compatibility adapter, while this collaborator keeps map
    // teardown, control-lane draining, and entity presentation ordering in a
    // single bounded lifecycle component.
    class PresentationLifecycleCoordinator final
    {
    public:
        bool Process(MultiplayerRuntimeGraph& graph);
        void Reset() noexcept;

    private:
        void InvalidateRemotePlayerState(
            MultiplayerRuntimeGraph& graph) noexcept;

        std::string departingEntityMap_;
        std::uint16_t departingEntityMapId_ = 0;
        std::uint16_t ignoredDepartingEntityMapId_ = 0;
        bool sourceMapFinalDrainRequired_ = false;
        std::size_t reportedRemotePlayerCount_ = 0;
    };
}
