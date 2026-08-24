#include "AutomationRunner.h"

#include "Automation/LocalInstance/MapTransitionAcceptanceDriver.h"
#include "Automation/Multiplayer/Abilities/HeroWillAbilityAcceptanceDriver.h"
#include "Automation/Multiplayer/Combat/CombatTargetAcceptanceDriver.h"
#include "Automation/Multiplayer/Combat/CombatVisualExchangeDriver.h"
#include "Automation/Multiplayer/Transition/NpcTransferAcceptanceDriver.h"
#include "Automation/Runtime/RuntimeConfiguration.h"
#include "Game/Runtime/GameServiceRuntime.h"
#include "Multiplayer/Runtime/MultiplayerSession.h"

#include <memory>

namespace fable::automation::runtime
{
    class AutomationRunner::State final
    {
    public:
        ::fable::automation::local_instance::MapTransitionAcceptanceDriver transition;
        ::fable::automation::multiplayer::combat::CombatTargetAcceptanceDriver combatTarget;
        ::fable::automation::multiplayer::combat::CombatVisualExchangeDriver combatVisual;
        ::fable::automation::multiplayer::abilities::HeroWillAbilityAcceptanceDriver heroWill;
        ::fable::automation::multiplayer::transition::NpcTransferAcceptanceDriver npcTransfer;
        ::fable::multiplayer::MultiplayerSession* multiplayer = nullptr;
        ::fable::core::Diagnostics diagnostics = {};
        bool heroWillFocused = false;
    };

    AutomationRunner::AutomationRunner()
        : state_(std::make_unique<State>())
    {
    }

    AutomationRunner::~AutomationRunner()
    {
        Shutdown();
    }

    bool AutomationRunner::Initialize(
        const RuntimeConfiguration& configuration,
        ::fable::game::GameServiceRuntime& services,
        ::fable::multiplayer::MultiplayerSession& multiplayer,
        const ::fable::core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        state_->multiplayer = &multiplayer;
        state_->diagnostics = diagnostics;

        state_->transition.Initialize(
            configuration.ScenarioIs(L"multiplayer_host_transition") ||
                configuration.ScenarioIs(L"multiplayer_host_authority") ||
                configuration.ScenarioIs(L"multiplayer_guest_transition"),
            configuration.ScenarioIs(L"multiplayer_host_authority"),
            services.Entities(),
            services.Locomotion(),
            diagnostics);

        const bool hostCombat = configuration.ScenarioIs(L"multiplayer_host_combat");
        const bool guestCombat = configuration.ScenarioIs(L"multiplayer_guest_combat");
        const bool hostWill = configuration.ScenarioIs(L"multiplayer_host_hero_will");
        const bool guestWill = configuration.ScenarioIs(L"multiplayer_guest_hero_will");
        const bool combat = hostCombat || guestCombat;
        const bool manualCombat = combat && configuration.ManualPlaytest();
        heroWillFocusedAcceptance_ = hostWill || guestWill;
        state_->heroWillFocused = heroWillFocusedAcceptance_;
        const bool anyWill = combat || heroWillFocusedAcceptance_;
        const bool acceptanceHost = hostCombat || hostWill;

        state_->combatTarget.Initialize(
            anyWill,
            acceptanceHost,
            heroWillFocusedAcceptance_ || manualCombat,
            services.Entities(),
            services.Creatures(),
            services.Combat(),
            services.Npcs(),
            diagnostics);
        if (manualCombat)
        {
            // Manual acceptance owns its own pacing. Keep the Hobbe fully
            // damageable and mortal so the tester can verify ordered health
            // and death replication without the automated fixture healing it.
            state_->combatTarget.AllowTargetDeath();
        }
        state_->combatVisual.Initialize(
            combat && !manualCombat,
            hostCombat,
            services.Entities(),
            services.Creatures(),
            services.Combat(),
            diagnostics);
        state_->heroWill.Initialize(
            anyWill && !manualCombat,
            acceptanceHost,
            heroWillFocusedAcceptance_,
            services.Entities(),
            services.HeroWill(),
            diagnostics);
        state_->npcTransfer.Initialize(
            configuration.ScenarioIs(L"multiplayer_host_transition"),
            services.Entities(),
            services.Npcs(),
            multiplayer,
            diagnostics);
        return true;
    }

    void AutomationRunner::Tick(float deltaSeconds, bool remotePresentationReady) noexcept
    {
        state_->npcTransfer.Tick(remotePresentationReady);
        state_->transition.Tick(deltaSeconds, remotePresentationReady);
        if (state_->combatVisual.WantsTargetDeath())
        {
            state_->combatTarget.AllowTargetDeath();
        }
        state_->combatTarget.Tick(remotePresentationReady);
        state_->combatVisual.Tick(remotePresentationReady);
        state_->heroWill.Tick(
            remotePresentationReady,
            heroWillFocusedAcceptance_
                ? state_->combatTarget.IsTargetReady()
                : state_->combatVisual.IsComplete());
    }

    void AutomationRunner::Shutdown() noexcept
    {
        if (state_ == nullptr)
        {
            return;
        }
        state_->npcTransfer.Shutdown();
        state_->heroWill.Shutdown();
        state_->combatVisual.Shutdown();
        state_->combatTarget.Shutdown();
        state_->transition.Shutdown();
        state_->multiplayer = nullptr;
        state_->diagnostics = {};
        heroWillFocusedAcceptance_ = false;
    }

    bool AutomationRunner::IsTargetReady() const noexcept
    {
        return state_ != nullptr && state_->combatTarget.IsTargetReady();
    }

    bool AutomationRunner::IsCombatComplete() const noexcept
    {
        return state_ != nullptr && state_->combatVisual.IsComplete();
    }
}
