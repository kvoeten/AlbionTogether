#include "RemotePlayerPartyHud.h"

#include "UI/Hud/HudService.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    // Retail Hero portrait art is a better neutral stand-in than the generic
    // follower glyph until a safe runtime portrait capture path exists.
    constexpr char HeroTexture[] = "HUD_HERO_ICON";
    constexpr float BarScale = 1.0f;
    constexpr std::uint64_t RetryDelayMilliseconds = 1000;
    constexpr std::uint64_t WorldTransitionSettleMilliseconds = 1000;
    constexpr fable::ui::HudColour FilledColour = { 62, 174, 75, 255 };
    constexpr fable::ui::HudColour EmptyColour = { 31, 66, 35, 255 };
}

namespace fable::multiplayer::presentation
{
    void RemotePlayerPartyHud::Initialize(
        ui::HudService& hud,
        const core::Diagnostics& diagnostics,
        const std::uint64_t localActorId) noexcept
    {
        Shutdown();
        hud_ = &hud;
        diagnostics_ = diagnostics;
        localActorId_ = localActorId;
    }

    void RemotePlayerPartyHud::Reconcile(
        const std::vector<replication::RemotePlayerSnapshot>& snapshots)
    {
        if (hud_ == nullptr)
        {
            return;
        }

        for (const replication::RemotePlayerSnapshot& snapshot : snapshots)
        {
            const PlayerState& state = snapshot.state;
            if (state.actorId == 0 || state.actorId == localActorId_ ||
                state.playerId.empty() || !snapshot.lifecycle.active)
            {
                continue;
            }

            Entry& entry = entries_[state.actorId];
            if (entry.actorGeneration != 0 &&
                (entry.actorGeneration != snapshot.ActorGeneration() ||
                 entry.playerId != state.playerId))
            {
                const bool samePlayer = entry.playerId == state.playerId;
                const float retainedCurrent = entry.currentHealth;
                const float retainedMaximum = entry.maximumHealth;
                RemoveNative(state.actorId, entry);
                entry = {};
                if (samePlayer && std::isfinite(retainedCurrent) &&
                    std::isfinite(retainedMaximum) &&
                    retainedMaximum > 0.0f)
                {
                    entry.currentHealth = std::clamp(
                        retainedCurrent, 0.0f, retainedMaximum);
                    entry.maximumHealth = retainedMaximum;
                }
            }
            entry.playerId = state.playerId;
            entry.actorGeneration = snapshot.ActorGeneration();
            EnsureVisible(state.actorId, entry);
        }

        for (auto iterator = entries_.begin(); iterator != entries_.end();)
        {
            const bool stillConnected = std::any_of(
                snapshots.begin(), snapshots.end(),
                [actorId = iterator->first](
                    const replication::RemotePlayerSnapshot& snapshot)
                {
                    return snapshot.state.actorId == actorId &&
                        snapshot.lifecycle.active;
                });
            if (stillConnected)
            {
                ++iterator;
                continue;
            }
            RemoveNative(iterator->first, iterator->second);
            iterator = entries_.erase(iterator);
        }
    }

    void RemotePlayerPartyHud::UpdateHealth(
        const std::uint64_t actorId,
        const float currentHealth,
        const float maximumHealth,
        const std::uint32_t revision)
    {
        const auto iterator = entries_.find(actorId);
        if (iterator == entries_.end() || !std::isfinite(currentHealth) ||
            !std::isfinite(maximumHealth) || maximumHealth <= 0.0f ||
            revision <= iterator->second.healthRevision)
        {
            return;
        }

        Entry& entry = iterator->second;
        entry.currentHealth = std::clamp(
            currentHealth, 0.0f, maximumHealth);
        entry.maximumHealth = maximumHealth;
        entry.healthRevision = revision;
        if (entry.elementId < 0 || worldTransitionActive_)
        {
            return;
        }
        if (!hud_->UpdateHealthBar(
                entry.elementId,
                entry.currentHealth,
                entry.maximumHealth,
                BarScale))
        {
            RemoveNative(actorId, entry);
            entry.nextAddAttemptAt = GetTickCount64() +
                RetryDelayMilliseconds;
        }
    }

    void RemotePlayerPartyHud::EnsureVisible(
        const std::uint64_t actorId,
        Entry& entry)
    {
        if (worldTransitionActive_ || entry.elementId >= 0)
        {
            return;
        }
        const std::uint64_t now = GetTickCount64();
        if (now < nativeHudReadyAt_ || now < entry.nextAddAttemptAt)
        {
            return;
        }

        int elementId = -1;
        if (!hud_->AddHealthBar(
                entry.currentHealth,
                entry.maximumHealth,
                FilledColour,
                EmptyColour,
                HeroTexture,
                entry.playerId,
                BarScale,
                elementId))
        {
            entry.nextAddAttemptAt = now + RetryDelayMilliseconds;
            if (!entry.failureReported)
            {
                char detail[192] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "actor_id=%llu player=%s",
                    static_cast<unsigned long long>(actorId),
                    entry.playerId.c_str());
                diagnostics_.Event(
                    "MultiplayerRemotePartyHudAddFailed", detail);
                entry.failureReported = true;
            }
            return;
        }

        entry.elementId = elementId;
        entry.failureReported = false;
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu player=%s generation=%u health=%.3f maximum=%.3f element_id=%d",
            static_cast<unsigned long long>(actorId),
            entry.playerId.c_str(),
            entry.actorGeneration,
            entry.currentHealth,
            entry.maximumHealth,
            entry.elementId);
        diagnostics_.Event("MultiplayerRemotePartyHudAdded", detail);
    }

    void RemotePlayerPartyHud::RemoveNative(
        const std::uint64_t actorId,
        Entry& entry) noexcept
    {
        if (hud_ == nullptr || entry.elementId < 0)
        {
            entry.elementId = -1;
            return;
        }
        const int elementId = entry.elementId;
        entry.elementId = -1;
        (void)hud_->RemoveElement(elementId);
        char detail[160] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu element_id=%d",
            static_cast<unsigned long long>(actorId),
            elementId);
        diagnostics_.Event("MultiplayerRemotePartyHudRemoved", detail);
    }

    void RemotePlayerPartyHud::Remove(const std::uint64_t actorId) noexcept
    {
        const auto iterator = entries_.find(actorId);
        if (iterator == entries_.end())
        {
            return;
        }
        RemoveNative(actorId, iterator->second);
        entries_.erase(iterator);
    }

    void RemotePlayerPartyHud::BeginWorldTransition() noexcept
    {
        worldTransitionActive_ = true;
        for (auto& [actorId, entry] : entries_)
        {
            RemoveNative(actorId, entry);
        }
    }

    void RemotePlayerPartyHud::CompleteWorldTransition() noexcept
    {
        worldTransitionActive_ = false;
        // Quest-info removals are native queued UI actions. Recreating the
        // bars in the same destination-completion frame can race that deferred
        // teardown and leave Fable's UI spatial list holding a stale node.
        nativeHudReadyAt_ = GetTickCount64() +
            WorldTransitionSettleMilliseconds;
        for (auto& [actorId, entry] : entries_)
        {
            (void)actorId;
            entry.nextAddAttemptAt = nativeHudReadyAt_;
        }
    }

    void RemotePlayerPartyHud::Shutdown() noexcept
    {
        for (auto& [actorId, entry] : entries_)
        {
            RemoveNative(actorId, entry);
        }
        entries_.clear();
        hud_ = nullptr;
        diagnostics_ = {};
        localActorId_ = 0;
        nativeHudReadyAt_ = 0;
        worldTransitionActive_ = false;
    }
}
