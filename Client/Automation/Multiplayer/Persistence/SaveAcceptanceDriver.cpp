#include "SaveAcceptanceDriver.h"

#include "Game/Entity/EntityService.h"
#include "Game/Persistence/Native/AutoSaveFunction.h"
#include "Game/Player/PlayerService.h"
#include "Multiplayer/Runtime/MultiplayerRuntimeGraph.h"

#include <Windows.h>

#include <cstdio>

namespace fable::automation::multiplayer::persistence
{
    void SaveAcceptanceDriver::Initialize(
        const bool enabled,
        const bool host,
        game::EntityService& entities,
        game::PlayerService& players,
        ::fable::multiplayer::MultiplayerRuntimeGraph& multiplayer,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        enabled_ = enabled;
        if (!enabled_)
        {
            return;
        }
        host_ = host;
        entities_ = &entities;
        players_ = &players;
        multiplayer_ = &multiplayer;
        diagnostics_ = diagnostics;
        diagnostics_.Event(
            "MultiplayerSaveAcceptanceArmed",
            host_ ? "role=host" : "role=guest");
    }

    void SaveAcceptanceDriver::Tick(
        const bool remotePresentationReady) noexcept
    {
        if (!enabled_ || failed_ || entities_ == nullptr ||
            multiplayer_ == nullptr)
        {
            return;
        }

        if (invoked_)
        {
            game::persistence::native::AutoSaveState state;
            if (game::persistence::native::AutoSaveFunction::ReadState(
                    *entities_, state) &&
                (state.saveState != lastSaveState_ ||
                    state.loadState != lastLoadState_))
            {
                lastSaveState_ = state.saveState;
                lastLoadState_ = state.loadState;
                char detail[128] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "role=%s save_state=%d load_state=%d",
                    host_ ? "host" : "guest",
                    state.saveState,
                    state.loadState);
                diagnostics_.Event("MultiplayerAutoSaveStateChanged", detail);
            }
            return;
        }

        const auto& contexts = multiplayer_->Contexts();
        const bool localWorldReady =
            contexts.players.localHero.IsWorldReady() &&
            contexts.players.localHero.CurrentState() != nullptr &&
            !contexts.players.localHero.MapName().empty();
        if (remotePresentationReady && localWorldReady &&
            contexts.players.remotePlayers.ActiveCount() != 0)
        {
            multiplayerWorldObserved_ = true;
        }
        if (!multiplayerWorldObserved_ || !localWorldReady)
        {
            readyAt_ = 0;
            return;
        }

        const std::uint64_t now = GetTickCount64();
        if (readyAt_ == 0)
        {
            readyAt_ = now + StableWorldMilliseconds +
                (host_ ? 0 : GuestSaveDelayMilliseconds);
            return;
        }
        if (now < readyAt_ ||
            saveQueued_.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }

        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "role=%s map=%s boundary=game-thread-idle",
            host_ ? "host" : "guest",
            contexts.players.localHero.MapName().c_str());
        diagnostics_.Event("MultiplayerAutoSaveQueued", detail);
    }

    bool SaveAcceptanceDriver::ProcessGameThreadIdle() noexcept
    {
        if (!enabled_ || invoked_ || failed_ || entities_ == nullptr ||
            players_ == nullptr ||
            !saveQueued_.exchange(false, std::memory_order_acq_rel))
        {
            return false;
        }

        if (!heroMutated_)
        {
            const float before = players_->GetHealth();
            const float maximum = players_->GetMaximumHealth();
            if (before <= 0.0f || maximum <= 1.0f)
            {
                readyAt_ = GetTickCount64() + RetryMilliseconds;
                return false;
            }

            const float after = before >= maximum - 0.5f
                ? maximum - 1.0f
                : (before + 1.0f <= maximum ? before + 1.0f : before - 1.0f);
            if (after <= 0.0f || !players_->SetHealth(after))
            {
                readyAt_ = GetTickCount64() + RetryMilliseconds;
                return false;
            }

            heroMutated_ = true;
            readyAt_ = GetTickCount64() + RetryMilliseconds;
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "role=%s health_before=%.3f health_after=%.3f health_maximum=%.3f",
                host_ ? "host" : "guest",
                before,
                after,
                maximum);
            diagnostics_.Event("MultiplayerSaveHeroMutationApplied", detail);
            return true;
        }

        const bool invoked =
            game::persistence::native::AutoSaveFunction::Invoke(*entities_);
        if (!invoked)
        {
            ++attempts_;
            if (attempts_ < MaximumAttempts)
            {
                readyAt_ = GetTickCount64() + RetryMilliseconds;
                if (attempts_ == 1)
                {
                    diagnostics_.Event(
                        "MultiplayerAutoSaveDeferred",
                        host_ ? "role=host reason=native-save-not-currently-safe" :
                            "role=guest reason=native-save-not-currently-safe");
                }
                return false;
            }

            failed_ = true;
            diagnostics_.Event(
                "MultiplayerAutoSaveFailed",
                host_ ? "role=host attempts=30" : "role=guest attempts=30");
            diagnostics_.Event("ClientFailed", "multiplayer-native-autosave-refused");
            return false;
        }

        invoked_ = true;
        game::persistence::native::AutoSaveState state;
        if (game::persistence::native::AutoSaveFunction::ReadState(
                *entities_, state))
        {
            lastSaveState_ = state.saveState;
            lastLoadState_ = state.loadState;
            char detail[160] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "role=%s boundary=game-thread-idle save_state=%d load_state=%d",
                host_ ? "host" : "guest",
                state.saveState,
                state.loadState);
            diagnostics_.Event("MultiplayerAutoSaveInvoked", detail);
            return true;
        }
        diagnostics_.Event(
            "MultiplayerAutoSaveInvoked",
            host_ ? "role=host boundary=game-thread-idle" :
                "role=guest boundary=game-thread-idle");
        return true;
    }

    void SaveAcceptanceDriver::Shutdown() noexcept
    {
        entities_ = nullptr;
        players_ = nullptr;
        multiplayer_ = nullptr;
        diagnostics_ = {};
        readyAt_ = 0;
        saveQueued_.store(false, std::memory_order_release);
        attempts_ = 0;
        lastSaveState_ = -1;
        lastLoadState_ = -1;
        host_ = false;
        enabled_ = false;
        multiplayerWorldObserved_ = false;
        invoked_ = false;
        heroMutated_ = false;
        failed_ = false;
    }
}
