#include "RemotePlayerRegistry.h"

#include "Game/Entity/EntityService.h"
#include "Multiplayer/Presentation/RemotePlayerPresentation.h"

#include <algorithm>
#include <cstdio>
#include <unordered_set>

namespace fable::multiplayer::presentation
{
    bool RemotePlayerRegistry::Initialize(
        game::EntityService& entities,
        game::NpcService& npcs,
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        game::creature::look::CreatureLookService& look,
        game::creature::combat::CreatureCombatService& combat,
        const core::Diagnostics& diagnostics,
        std::uint64_t localActorId)
    {
        Shutdown();
        entities_ = &entities;
        npcs_ = &npcs;
        locomotion_ = &locomotion;
        look_ = &look;
        combat_ = &combat;
        diagnostics_ = diagnostics;
        localActorId_ = localActorId;
        if (!presentationFactory_.Install(entities.GameModule(), diagnostics))
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-remote-hero-presentation-factory-hook");
            Shutdown();
            return false;
        }
        initialized_ = true;
        diagnostics_.Event(
            "MultiplayerRemotePlayerRegistryReady",
            "remote native presentations are dynamically keyed by actor lifecycle");
        return true;
    }

    std::unique_ptr<RemotePlayerPresentation>
        RemotePlayerRegistry::CreatePresentation()
    {
        auto presentation = std::make_unique<RemotePlayerPresentation>();
        if (!presentation->Initialize(
                *entities_, *npcs_, *locomotion_, *look_, *combat_, diagnostics_,
                presentationFactory_))
        {
            return nullptr;
        }
        return presentation;
    }

    void RemotePlayerRegistry::Reconcile(
        const std::vector<replication::RemotePlayerSnapshot>& snapshots,
        const std::string& localMap,
        game::Entity* localHero)
    {
        if (!initialized_)
        {
            return;
        }
        std::unordered_set<std::uint64_t> liveActors;
        liveActors.reserve(snapshots.size());
        for (const replication::RemotePlayerSnapshot& snapshot : snapshots)
        {
            const PlayerState& state = snapshot.state;
            if (state.actorId == 0 || state.actorId == localActorId_)
            {
                continue;
            }
            liveActors.insert(state.actorId);
            auto iterator = presentations_.find(state.actorId);
            if (iterator == presentations_.end())
            {
                if (presentationFactory_.IsArmed())
                {
                    continue;
                }
                auto presentation = CreatePresentation();
                if (presentation == nullptr)
                {
                    diagnostics_.Event(
                        "ClientFailed",
                        "multiplayer-remote-presentation-allocation");
                    continue;
                }
                iterator = presentations_.emplace(
                    state.actorId, std::move(presentation)).first;
                char detail[128] = {};
                std::snprintf(
                    detail, sizeof(detail), "actor_id=%llu count=%zu",
                    static_cast<unsigned long long>(state.actorId),
                    presentations_.size());
                diagnostics_.Event(
                    "MultiplayerRemotePlayerRegistered", detail);
            }
            iterator->second->Reconcile(
                state, localMap, localHero, snapshot.receivedAt);
        }

        // Channel removal is explicit (disconnect/authority retirement). A
        // player on another map remains live but its presentation is dormant.
        (void)liveActors;
    }

    void RemotePlayerRegistry::Remove(std::uint64_t actorId) noexcept
    {
        const auto iterator = presentations_.find(actorId);
        if (iterator == presentations_.end())
        {
            return;
        }
        iterator->second->Shutdown();
        presentations_.erase(iterator);
    }

    void RemotePlayerRegistry::BeginWorldTransition() noexcept
    {
        for (auto& [actorId, presentation] : presentations_)
        {
            (void)actorId;
            presentation->BeginWorldTransition();
        }
    }

    void RemotePlayerRegistry::CompleteWorldTransition() noexcept
    {
        for (auto& [actorId, presentation] : presentations_)
        {
            (void)actorId;
            presentation->CompleteWorldTransition();
        }
    }

    void RemotePlayerRegistry::DriveMovement()
    {
        for (auto& [actorId, presentation] : presentations_)
        {
            (void)actorId;
            presentation->DriveMovement();
        }
    }

    bool RemotePlayerRegistry::ApplyHealth(
        std::uint64_t actorId,
        float currentHealth,
        float maximumHealth,
        std::uint32_t revision)
    {
        const auto iterator = presentations_.find(actorId);
        return iterator != presentations_.end() &&
            iterator->second != nullptr &&
            iterator->second->ApplyHealth(
                currentHealth, maximumHealth, revision);
    }

    void RemotePlayerRegistry::Shutdown() noexcept
    {
        for (auto& [actorId, presentation] : presentations_)
        {
            (void)actorId;
            presentation->Shutdown();
        }
        presentations_.clear();
        presentationFactory_.Cancel();
        entities_ = nullptr;
        npcs_ = nullptr;
        locomotion_ = nullptr;
        look_ = nullptr;
        combat_ = nullptr;
        diagnostics_ = {};
        localActorId_ = 0;
        initialized_ = false;
    }

    std::size_t RemotePlayerRegistry::Size() const noexcept
    {
        return presentations_.size();
    }

    std::size_t RemotePlayerRegistry::ActiveCount() const
    {
        return static_cast<std::size_t>(std::count_if(
            presentations_.begin(),
            presentations_.end(),
            [](const auto& entry)
            {
                return entry.second != nullptr && entry.second->IsActive();
            }));
    }
}
