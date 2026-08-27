#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fable::ui
{
    class HudService;
}

namespace fable::multiplayer::presentation
{
    // Owns one native ally health bar per connected remote player. Entries are
    // tied to the reliable actor lifecycle, not local avatar materialization,
    // so friends remain visible while they explore another region.
    class RemotePlayerPartyHud final
    {
    public:
        void Initialize(
            ui::HudService& hud,
            const core::Diagnostics& diagnostics,
            std::uint64_t localActorId) noexcept;
        void Reconcile(
            const std::vector<replication::RemotePlayerSnapshot>& snapshots);
        void UpdateHealth(
            std::uint64_t actorId,
            float currentHealth,
            float maximumHealth,
            std::uint32_t revision);
        void Remove(std::uint64_t actorId) noexcept;
        void BeginWorldTransition() noexcept;
        void CompleteWorldTransition() noexcept;
        void Shutdown() noexcept;

    private:
        struct Entry final
        {
            std::string playerId;
            std::uint32_t actorGeneration = 0;
            std::uint32_t healthRevision = 0;
            std::uint64_t nextAddAttemptAt = 0;
            float currentHealth = 1.0f;
            float maximumHealth = 1.0f;
            int elementId = -1;
            bool failureReported = false;
        };

        void EnsureVisible(std::uint64_t actorId, Entry& entry);
        void RemoveNative(std::uint64_t actorId, Entry& entry) noexcept;

        ui::HudService* hud_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::unordered_map<std::uint64_t, Entry> entries_;
        std::uint64_t localActorId_ = 0;
        bool worldTransitionActive_ = false;
    };
}
