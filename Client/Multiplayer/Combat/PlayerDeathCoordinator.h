#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Combat/CombatHealthMutationEvent.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace fable::game
{
    class QuestService;
}

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::multiplayer::replication
{
    class LocalHeroReplication;
}

namespace fable::multiplayer::combat
{
    // Defers local Hero death handling out of the native health hook. Fable's
    // own mutation runs first, so resurrection phials retain their retail
    // consumption and revival behavior. A still-terminal Hero is restored
    // only after the registered Guild teleport quest accepts the fallback.
    class PlayerDeathCoordinator final
    {
    public:
        bool Initialize(
            game::creature::combat::CreatureCombatService& combat,
            game::QuestService& quests,
            const core::Diagnostics& diagnostics) noexcept;
        bool Process(
            const replication::LocalHeroReplication& localHero) noexcept;
        void Shutdown() noexcept;

    private:
        struct PendingDeath final
        {
            void* creature = nullptr;
            std::uint64_t thingUid = 0;
            std::uint64_t observedAt = 0;
            std::uint64_t nextAttemptAt = 0;
            unsigned int attempts = 0;
            bool reportedDeferred = false;
        };

        static constexpr std::size_t EventCapacity = 64;
        static constexpr float TerminalHealth = 0.01f;
        static constexpr const char* GuildTeleportQuest =
            "Global_TeleportToHeroGuild";

        static void CaptureMutation(
            void* context,
            const game::creature::combat::CombatHealthMutationEvent& event)
            noexcept;
        void Enqueue(
            const game::creature::combat::CombatHealthMutationEvent& event)
            noexcept;

        game::creature::combat::CreatureCombatService* combat_ = nullptr;
        game::QuestService* quests_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::mutex eventMutex_;
        std::deque<game::creature::combat::CombatHealthMutationEvent> events_;
        PendingDeath pending_;
        std::atomic_bool acceptingEvents_{false};
        std::atomic_uint droppedEvents_{0};
        bool initialized_ = false;
    };
}
