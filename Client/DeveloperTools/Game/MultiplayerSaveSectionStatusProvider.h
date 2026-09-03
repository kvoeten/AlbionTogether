#pragma once

#include "GameDeveloperToolAdapter.h"

namespace fable::multiplayer
{
    class MultiplayerRuntimeGraph;
}

namespace fable::developer_tools
{
    // Read-only projection for the host developer window. It reports the
    // current bounded replication snapshots; it never parses or mutates a
    // retail save section itself.
    class MultiplayerSaveSectionStatusProvider final
        : public IDeveloperWorldAuthority
    {
    public:
        void Bind(multiplayer::MultiplayerRuntimeGraph* multiplayer) noexcept;

        bool ReadSaveSectionStatus(
            DeveloperSaveSection section,
            std::uint64_t& fingerprint,
            DeveloperToolText& detail) const noexcept override;
        bool PublishQuestState() noexcept override;

    private:
        multiplayer::MultiplayerRuntimeGraph* multiplayer_ = nullptr;
    };
}
