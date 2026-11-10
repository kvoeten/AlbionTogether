#include "RemoteHeroActor.h"

#include "Game/Entity/Entity.h"
#include "RemoteHeroNativeLifecycle.h"

namespace fable::multiplayer::presentation::remote_hero
{
    using ::fable::multiplayer::PlayerState;
    namespace movement = ::fable::multiplayer::movement;

    RemoteHeroActor::RemoteHeroActor() = default;

    RemoteHeroActor::~RemoteHeroActor()
    {
        if (initialized_ || lifecycle_ != nullptr)
        {
            Shutdown();
        }
    }

    bool RemoteHeroActor::Initialize(
        game::EntityService& entities,
        game::NpcService& npcs,
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        game::creature::look::CreatureLookService& look,
        game::creature::animation::CreatureAnimationService& animation,
        game::creature::combat::CreatureCombatService& combat,
        game::hero_pawn::abilities::HeroWillAbilityService& abilities,
        multiplayer::combat::PlayerCombatantDirectory& combatants,
        const core::Diagnostics& diagnostics,
        game::hero_pawn::appearance::hooks::
            RemoteHeroDefinitionHook& definitionHook,
        game::hero_pawn::appearance::hooks::
            RemoteHeroPresentationFactoryHook& presentationFactory,
        game::hero_pawn::equipment::hooks::
            RemoteRangedWeaponOrientationHook& orientationHook)
    {
        Shutdown();
        entities_ = &entities;
        npcs_ = &npcs;
        look_ = &look;
        combatants_ = &combatants;
        diagnostics_ = diagnostics;
        definitionHook_ = &definitionHook;
        presentationFactory_ = &presentationFactory;
        movement_.Initialize(locomotion, diagnostics);
        appearance_.Initialize(diagnostics);
        equipment_.Initialize(
            entities, animation, orientationHook, diagnostics);
        combat_.Initialize(entities, combat, equipment_, diagnostics);
        abilities_.Initialize(entities, abilities, diagnostics);
        expressions_.Initialize(entities, animation, diagnostics);
        lifecycle_ = std::make_unique<RemoteHeroNativeLifecycle>(*this);
        initialized_ = true;
        return true;
    }

    bool RemoteHeroActor::ApplyHealth(
        float currentHealth,
        float maximumHealth,
        std::uint32_t revision)
    {
        return initialized_ && IsLifecycleActive() && !worldTransitionActive_ &&
            combat_.ApplyHealth(currentHealth, maximumHealth, revision);
    }

    bool RemoteHeroActor::PerformAbility(
        game::creature::equipment::CreatureWeaponFamily weaponFamily,
        const game::hero_pawn::equipment::HeroWeaponDefinitions&
            requiredWeapons,
        std::uint32_t meleeAttachmentSlot,
        std::uint32_t rangedAttachmentSlot,
        std::uint32_t abilityId,
        float charge,
        void* targetCreature,
        const std::string& resolvedActionType,
        std::uint32_t resolvedAnimationId)
    {
        return initialized_ && IsLifecycleActive() && !worldTransitionActive_ &&
            combat_.PerformAbility(
                weaponFamily,
                requiredWeapons,
                meleeAttachmentSlot,
                rangedAttachmentSlot,
                abilityId,
                charge,
                targetCreature, resolvedActionType, resolvedAnimationId);
    }

    bool RemoteHeroActor::EndRangedAim() noexcept
    {
        return initialized_ && IsLifecycleActive() && !worldTransitionActive_ &&
            combat_.EndRangedAim();
    }

    bool RemoteHeroActor::PerformWeaponTransition(
        game::creature::equipment::CreatureWeaponFamily weaponFamily,
        const game::hero_pawn::equipment::HeroWeaponDefinitions&
            requiredWeapons,
        std::uint32_t meleeAttachmentSlot,
        std::uint32_t rangedAttachmentSlot,
        const std::string& resolvedActionType,
        std::uint32_t resolvedAnimationId,
        std::uint64_t actionId)
    {
        game::hero_pawn::equipment::HeroEquipmentState equipment;
        equipment.valid = true;
        equipment.meleeDefinitionIndex =
            requiredWeapons.meleeDefinitionIndex;
        equipment.rangedDefinitionIndex =
            requiredWeapons.rangedDefinitionIndex;
        equipment.meleeAttachmentSlot = meleeAttachmentSlot;
        equipment.rangedAttachmentSlot = rangedAttachmentSlot;
        equipment.activeFamily = weaponFamily;
        equipment.transitionActionId = actionId;
        if (!initialized_ || !IsLifecycleActive() || worldTransitionActive_)
        {
            return false;
        }
        if (weaponFamily != game::creature::equipment::
                CreatureWeaponFamily::Ranged)
        {
            (void)combat_.EndRangedAim();
        }
        return equipment_.PerformTransition(
            equipment, resolvedActionType, resolvedAnimationId, actionId);
    }

    bool RemoteHeroActor::IsWeaponTransitionPending() const noexcept
    {
        return initialized_ && IsLifecycleActive() && !worldTransitionActive_ &&
            equipment_.IsTransitionPending();
    }

    bool RemoteHeroActor::PerformHeroAbility(
        game::hero_pawn::abilities::HeroAbility ability,
        game::hero_pawn::abilities::HeroAbilityCommand command,
        std::int32_t progressionState,
        void* targetCreature)
    {
        return initialized_ && IsLifecycleActive() && !worldTransitionActive_ &&
            abilities_.Perform(
                ability, command, progressionState, targetCreature);
    }

    bool RemoteHeroActor::PerformExpression(
        const std::string& expressionDefinition,
        void* targetCreature,
        const std::string& resolvedActionType,
        const std::uint32_t resolvedAnimationId,
        const std::int32_t expressionDurationTicks,
        const std::int32_t expressionTriggerTicks)
    {
        if (!initialized_ || !IsLifecycleActive() || worldTransitionActive_ ||
            nativeAvatar_ == nullptr || expressionDefinition.empty())
        {
            return false;
        }
        return expressions_.Perform(
            nativeAvatar_,
            targetCreature,
            expressionDefinition,
            resolvedActionType,
            resolvedAnimationId,
            expressionDurationTicks,
            expressionTriggerTicks);
    }

    movement::ReplicatedMovementSample
        RemoteHeroActor::MovementSample(
            const PlayerState& state,
            std::uint64_t receivedAt)
    {
        movement::ReplicatedMovementSample sample;
        sample.actorId = state.actorId;
        sample.authorityEpoch = state.authorityEpoch;
        sample.sequence = state.sequence;
        sample.mapName = state.mapName;
        sample.position = state.position;
        sample.velocity = state.velocity;
        sample.facing = state.facing;
        sample.angularVelocity = state.angularVelocity;
        sample.moving = state.moving;
        // The transport projects the owner-authored session timestamp onto
        // this receiver's monotonic clock. Fall back to arrival time during
        // the brief pre-synchronization window.
        sample.receivedAt = state.movementSampleAt != 0
            ? state.movementSampleAt
            : receivedAt;
        return sample;
    }

    bool RemoteHeroActor::ReadMovement(
        void* context,
        void* creature,
        movement::ReplicatedActorMovement::NativeInput& input)
    {
        auto* const presentation =
            static_cast<RemoteHeroActor*>(context);
        return presentation != nullptr && presentation->IsMovementReady() &&
            presentation->movement_.Provide(creature, input);
    }

    void RemoteHeroActor::BeginWorldTransition() noexcept
    {
        worldTransitionActive_ = true;
        if (lifecycle_ != nullptr)
        {
            lifecycle_->Retire();
        }
        diagnostics_.Event(
            "MultiplayerRemoteWorldPresentationRetired",
            "source native presentation released before world teardown; destination awaits its complete current baseline");
    }

    void RemoteHeroActor::DriveMovement()
    {
        if (lifecycle_ != nullptr)
        {
            lifecycle_->DriveMovement();
        }
    }

    bool RemoteHeroActor::IsMovementReady() const
    {
        return initialized_ &&
            lifecyclePhase_ != RemoteHeroLifecyclePhase::Constructing &&
            !worldTransitionActive_ && avatar_ != nullptr && avatar_->IsValid();
    }

    bool RemoteHeroActor::IsActive() const
    {
        return IsLifecycleActive() && avatar_ != nullptr &&
            !worldTransitionActive_ && avatar_->IsValid();
    }

    bool RemoteHeroActor::MatchesLifecycle(
        std::uint32_t actorGeneration,
        std::uint32_t mapEpoch) const noexcept
    {
        return actorGeneration_ == actorGeneration && mapEpoch_ == mapEpoch;
    }

    void RemoteHeroActor::CompleteWorldTransition() noexcept
    {
        worldTransitionActive_ = false;
        nextSpawnAttemptAt_ = 0;
    }

    void RemoteHeroActor::Shutdown() noexcept
    {
        if (lifecycle_ != nullptr)
        {
            lifecycle_->Retire();
        }
        movement_.Detach();
        abilities_.Shutdown();
        expressions_.Shutdown();
        combat_.Shutdown();
        equipment_.Shutdown();
        appearance_.Shutdown();
        lifecycle_.reset();
        entities_ = nullptr;
        npcs_ = nullptr;
        look_ = nullptr;
        combatants_ = nullptr;
        definitionHook_ = nullptr;
        presentationFactory_ = nullptr;
        diagnostics_ = {};
        nextSpawnAttemptAt_ = 0;
        actorId_ = 0;
        actorGeneration_ = 0;
        mapEpoch_ = 0;
        worldTransitionActive_ = false;
        initialized_ = false;
    }
}

