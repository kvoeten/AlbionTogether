#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::developer_tools
{
    constexpr std::size_t DeveloperToolTextCapacity = 96;
    constexpr std::size_t DeveloperToolQueueCapacity = 64;

    struct DeveloperToolText final
    {
        std::array<char, DeveloperToolTextCapacity> value{};

        static DeveloperToolText From(const char* text) noexcept;
        bool Empty() const noexcept;
    };

    struct DeveloperToolPosition final
    {
        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;
    };

    enum class DeveloperCommandKind : std::uint8_t
    {
        SpawnEntity,
        TeleportEntity,
        UseRegionExit,
        QueryQuest,
        ActivateQuest,
        QuerySaveSection
    };

    enum class DeveloperSaveSection : std::uint8_t
    {
        Entities,
        Player,
        Quests,
        Regions,
        Factions
    };

    struct SpawnEntityCommand final
    {
        DeveloperToolText definition;
        DeveloperToolPosition position;
    };

    struct TeleportEntityCommand final
    {
        std::uint64_t entityUid = 0;
        DeveloperToolPosition position;
    };

    struct UseRegionExitCommand final
    {
        std::uint64_t exitUid = 0;
    };

    struct QuestCommand final
    {
        DeveloperToolText questName;
    };

    struct ActivateQuestCommand final
    {
        DeveloperToolText questName;
    };

    struct SaveSectionCommand final
    {
        DeveloperSaveSection section = DeveloperSaveSection::Entities;
    };
}
