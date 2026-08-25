#include "PlayerDeathCoordinator.h"
#include "PlayerDeathPolicy.h"

#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Quest/QuestService.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"

#include <Windows.h>

#include <cstdio>

namespace fable::multiplayer::combat
{
    bool PlayerDeathCoordinator::Initialize(
        game::creature::combat::CreatureCombatService& combat,
        game::QuestService& quests,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        combat_ = &combat;
        quests_ = &quests;
        diagnostics_ = diagnostics;
        acceptingEvents_.store(true, std::memory_order_release);
        if (!combat_->AddHealthMutationSink(
                &PlayerDeathCoordinator::CaptureMutation, this))
        {
            acceptingEvents_.store(false, std::memory_order_release);
            combat_ = nullptr;
            quests_ = nullptr;
            diagnostics_ = {};
            return false;
        }
        initialized_ = true;
        diagnostics_.Event(
            "MultiplayerPlayerDeathReady",
            "native resurrection phials remain authoritative; terminal Heroes use the retail Guild teleport quest");
        return true;
    }

    bool PlayerDeathCoordinator::Process(
        const replication::LocalHeroReplication& localHero) noexcept
    {
        if (!initialized_ || combat_ == nullptr || quests_ == nullptr)
        {
            return false;
        }

        std::deque<game::creature::combat::CombatHealthMutationEvent> captured;
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            captured.swap(events_);
        }
        void* const localCreature = localHero.NativeHero();
        for (const auto& event : captured)
        {
            if (localCreature == nullptr || event.creature != localCreature)
            {
                continue;
            }
            const PlayerDeathOutcome outcome = ClassifyPlayerDeath(
                event.currentHealth, event.maximumHealth);
            if (outcome == PlayerDeathOutcome::Alive)
            {
                pending_ = {};
                continue;
            }
            if (outcome == PlayerDeathOutcome::GuildRespawnRequired &&
                pending_.creature != event.creature)
            {
                pending_.creature = event.creature;
                pending_.thingUid = event.thingUid;
                pending_.observedAt = event.observedAt;
                pending_.nextAttemptAt = event.observedAt;
            }
        }
        if (pending_.creature == nullptr)
        {
            return true;
        }
        if (pending_.creature != localCreature)
        {
            pending_ = {};
            return true;
        }

        float currentHealth = 0.0f;
        float maximumHealth = 0.0f;
        if (!combat_->ReadCombatHealth(
                localCreature, currentHealth, maximumHealth))
        {
            return true;
        }
        if (currentHealth > TerminalHealth)
        {
            diagnostics_.Event(
                "MultiplayerPlayerNativeResurrectionObserved",
                "local Hero recovered before Guild fallback; native inventory behavior won");
            pending_ = {};
            return true;
        }

        const std::uint64_t now = GetTickCount64();
        if (now < pending_.nextAttemptAt)
        {
            return true;
        }
        ++pending_.attempts;
        pending_.nextAttemptAt = now +
            (pending_.attempts < 8 ? 250u : 1'000u);

        const bool registered = quests_->IsRegistered(GuildTeleportQuest);
        const bool accepted = registered &&
            (quests_->IsActive(GuildTeleportQuest) ||
                quests_->Activate(GuildTeleportQuest));
        if (!accepted)
        {
            if (!pending_.reportedDeferred)
            {
                char detail[224] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "thing_uid=%016llX quest=%s registered=%s; retrying outside the health hook",
                    static_cast<unsigned long long>(pending_.thingUid),
                    GuildTeleportQuest,
                    registered ? "true" : "false");
                diagnostics_.Event(
                    "MultiplayerPlayerGuildRespawnDeferred", detail);
                pending_.reportedDeferred = true;
            }
            return true;
        }

        const float healing = maximumHealth > currentHealth
            ? maximumHealth - currentHealth
            : 0.0f;
        if (healing <= TerminalHealth ||
            !combat_->ApplyOwnedCombatHealing(localCreature, healing))
        {
            return true;
        }
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX quest=%s restored_health=%.3f attempts=%u",
            static_cast<unsigned long long>(pending_.thingUid),
            GuildTeleportQuest,
            maximumHealth,
            pending_.attempts);
        diagnostics_.Event("MultiplayerPlayerGuildRespawnStarted", detail);
        pending_ = {};
        return true;
    }

    void PlayerDeathCoordinator::Shutdown() noexcept
    {
        acceptingEvents_.store(false, std::memory_order_release);
        if (combat_ != nullptr)
        {
            combat_->RemoveHealthMutationSink(
                &PlayerDeathCoordinator::CaptureMutation, this);
        }
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            events_.clear();
        }
        pending_ = {};
        droppedEvents_.store(0, std::memory_order_release);
        combat_ = nullptr;
        quests_ = nullptr;
        diagnostics_ = {};
        initialized_ = false;
    }

    void PlayerDeathCoordinator::CaptureMutation(
        void* context,
        const game::creature::combat::CombatHealthMutationEvent& event)
        noexcept
    {
        if (context != nullptr)
        {
            static_cast<PlayerDeathCoordinator*>(context)->Enqueue(event);
        }
    }

    void PlayerDeathCoordinator::Enqueue(
        const game::creature::combat::CombatHealthMutationEvent& event)
        noexcept
    {
        if (!acceptingEvents_.load(std::memory_order_acquire))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(eventMutex_);
        if (events_.size() >= EventCapacity)
        {
            droppedEvents_.fetch_add(1, std::memory_order_acq_rel);
            return;
        }
        events_.push_back(event);
    }
}
