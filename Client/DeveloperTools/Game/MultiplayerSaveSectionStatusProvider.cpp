#include "MultiplayerSaveSectionStatusProvider.h"

#include "Multiplayer/Runtime/MultiplayerRuntimeGraph.h"

#include <cstdio>

namespace fable::developer_tools
{
    void MultiplayerSaveSectionStatusProvider::Bind(
        multiplayer::MultiplayerRuntimeGraph* const multiplayer) noexcept
    {
        multiplayer_ = multiplayer;
    }

    bool MultiplayerSaveSectionStatusProvider::ReadSaveSectionStatus(
        const DeveloperSaveSection section,
        std::uint64_t& fingerprint,
        DeveloperToolText& detail) const noexcept
    {
        fingerprint = 0;
        detail = {};
        if (multiplayer_ == nullptr || !multiplayer_->IsEnabled())
        {
            detail = DeveloperToolText::From("multiplayer world is not active");
            return false;
        }

        char text[DeveloperToolTextCapacity] = {};
        const auto& contexts = multiplayer_->Contexts();
        if (section == DeveloperSaveSection::Entities)
        {
            const auto& directory = contexts.world.savedEntityMapBaseline.Directory();
            fingerprint = directory.CaptureRevision();
            std::snprintf(text, sizeof(text),
                "ENTITIES revision=%llu maps=%zu bytes=%zu complete=%s",
                static_cast<unsigned long long>(fingerprint),
                directory.Size(), directory.TotalBytes(),
                directory.IsComplete() ? "yes" : "no");
            detail = DeveloperToolText::From(text);
            return true;
        }
        if (section == DeveloperSaveSection::Player)
        {
            const auto* const player = contexts.transport.localPlayerChannel.CurrentState();
            if (player == nullptr)
            {
                detail = DeveloperToolText::From("PLAYER Hero state is not bound");
                return false;
            }
            fingerprint = player->actorId ^
                (static_cast<std::uint64_t>(player->actorGeneration) << 32U) ^
                player->mapEpoch;
            std::snprintf(text, sizeof(text),
                "PLAYER local Hero map=%s generation=%u map_epoch=%u",
                player->mapName.c_str(), player->actorGeneration,
                player->mapEpoch);
            detail = DeveloperToolText::From(text);
            return true;
        }
        if (section == DeveloperSaveSection::Quests)
        {
            const auto& quests = contexts.world.questState;
            if (!quests.HasCurrentSnapshot())
            {
                detail = DeveloperToolText::From("QUESTS snapshot is not captured yet");
                return false;
            }
            fingerprint = quests.CurrentSnapshotFingerprint();
            std::snprintf(text, sizeof(text),
                "QUESTS revision=%llu bytes=%zu hash=%016llX",
                static_cast<unsigned long long>(
                    quests.CurrentSnapshotRevision()),
                quests.CurrentSnapshotBytes(),
                static_cast<unsigned long long>(fingerprint));
            detail = DeveloperToolText::From(text);
            return true;
        }

        const auto worldSection = section == DeveloperSaveSection::Regions
            ? multiplayer::protocol::WorldSection::Regions
            : multiplayer::protocol::WorldSection::Factions;
        const auto& worldSections = contexts.world.worldSections;
        if (!worldSections.HasCurrentSnapshot(worldSection))
        {
            detail = DeveloperToolText::From(
                section == DeveloperSaveSection::Regions
                    ? "REGIONS snapshot is not captured yet"
                    : "FACTIONS snapshot is not captured yet");
            return false;
        }
        fingerprint = worldSections.CurrentSnapshotFingerprint(worldSection);
        std::snprintf(text, sizeof(text),
            "%s revision=%llu bytes=%zu hash=%016llX",
            section == DeveloperSaveSection::Regions
                ? "REGIONS" : "FACTIONS",
            static_cast<unsigned long long>(
                worldSections.CurrentSnapshotRevision(worldSection)),
            worldSections.CurrentSnapshotBytes(worldSection),
            static_cast<unsigned long long>(fingerprint));
        detail = DeveloperToolText::From(text);
        return true;
    }

    bool MultiplayerSaveSectionStatusProvider::PublishQuestState() noexcept
    {
        return multiplayer_ != nullptr && multiplayer_->IsEnabled() &&
            multiplayer_->Contexts().world.authority.IsHost() &&
            multiplayer_->Contexts().world.questState.CaptureHostCurrent();
    }
}
