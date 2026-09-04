#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fable::multiplayer
{
    class MultiplayerRuntimeGraph;

    // Owns the ordered per-frame world/presentation phases. The session
    // remains the compatibility adapter, while this collaborator keeps map
    // teardown, control-lane draining, quest progression sampling, and entity
    // presentation ordering in a single bounded lifecycle component.
    class PresentationLifecycleCoordinator final
    {
    public:
        bool Process(MultiplayerRuntimeGraph& graph);
        void Reset() noexcept;

    private:
        static constexpr std::uint64_t QuestProgressionCaptureIntervalMilliseconds =
            1'000;

        void InvalidateRemotePlayerState(
            MultiplayerRuntimeGraph& graph) noexcept;
        void CaptureHostQuestProgression(
            MultiplayerRuntimeGraph& graph,
            std::uint64_t nowMilliseconds) noexcept;
        void ApplyGuestQuestProgression(
            MultiplayerRuntimeGraph& graph) noexcept;

        std::string departingEntityMap_;
        std::uint16_t departingEntityMapId_ = 0;
        std::uint16_t ignoredDepartingEntityMapId_ = 0;
        std::uint64_t lastQuestProgressionCaptureMilliseconds_ = 0;
        std::uint64_t lastQuestProgressionApplyAttemptRevision_ = 0;
        std::uint64_t lastQuestProgressionApplyAttemptFingerprint_ = 0;
        bool sourceMapFinalDrainRequired_ = false;
        std::size_t reportedRemotePlayerCount_ = 0;
    };
}
