#include "GameplayRuntimeState.h"

#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Creature/Locomotion/CreatureLocomotionService.h"

#include <algorithm>

namespace fable::game
{
    void GameplayRuntime::DispatchKeyPressed(unsigned int virtualKey, bool shiftPressed)
    {
        if (!state_->mailbox.KeyPressed(virtualKey, shiftPressed))
            state_->diagnostics.Event("ScriptInputQueueFull", "bounded key request rejected");
    }

    void GameplayRuntime::DispatchWorldReady() { state_->mailbox.WorldReady(); }
    bool GameplayRuntime::ConsumeWorldDeparture() { return state_->mailbox.ConsumeDeparture(); }
    void GameplayRuntime::RequestAutomationIdle() { state_->mailbox.AutomationIdle(); }
    void GameplayRuntime::QueueWindowTick(bool background) { state_->mailbox.Background(background); }

    bool GameplayRuntime::Reload()
    {
        state_->mailbox.Reload();
        return true; // Request accepted; execution/reporting belongs to the simulation.
    }

    void GameplayRuntime::ProcessSimulationFrame()
    {
        if (!state_->scriptsReady.load(std::memory_order_acquire)) return;
        const auto requests = state_->mailbox.Take();
        const ULONGLONG now = GetTickCount64();
        const float delta = state_->lastFrameAt == 0 ? 0.0f :
            static_cast<float>((std::min)(now - state_->lastFrameAt, 100ULL)) / 1000.0f;
        state_->lastFrameAt = now;

        if (requests.worldReady)
        {
            if (state_->multiplayer.OnWorldReady())
                state_->scripts.DispatchWorldReady();
            else
                state_->diagnostics.Event("ClientFailed", "multiplayer-world-entry");
        }

        // The load hook retains its own control-lane pump when native loading
        // blocks normal frames. No window-side reconciliation runs in parallel.
        if (state_->multiplayer.ProcessPresentationLifecycle())
        {
            state_->mailbox.Departed();
            return;
        }
        if (requests.automationIdle && state_->automation.ProcessGameThreadIdle()) return;

        if (requests.reload)
        {
            if (state_->multiplayer.IsEnabled())
                state_->diagnostics.Event("ScriptReloadSkipped",
                    "multiplayer owns shared locomotion and combat routes");
            else
            {
                state_->services.Combat().ClearPlayerCombat();
                state_->services.Locomotion().ClearHeroShadow();
                (void)state_->scripts.Reload();
            }
        }
        for (std::size_t index = 0; index < requests.keyCount; ++index)
            state_->scripts.DispatchKeyPressed(requests.keys[index].code, requests.keys[index].shift);

        state_->services.Locomotion().TickHeroShadow();
        state_->automation.Tick(delta, state_->multiplayer.HasActiveRemotePresentation());
        state_->scripts.Tick(delta);
        state_->developerTools.Tick();
        // Preserve unfocused-client movement without modifying native physics
        // concurrently from a Windows timer. Focus is a latest-value input.
        if (requests.background) state_->multiplayer.DriveReplicatedMovement();
    }
}
