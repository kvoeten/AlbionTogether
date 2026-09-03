#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/HeroPawn/Appearance/Hooks/RemoteHeroPresentationFactoryHook.h"
#include "Game/HeroPawn/Appearance/Hooks/RemoteHeroDefinitionHook.h"
#include "Game/HeroPawn/Appearance/RemoteHeroAppearanceController.h"
#include "Game/Creature/Equipment/CreatureWeaponFamily.h"
#include "Game/HeroPawn/Equipment/RemoteHeroEquipmentController.h"
#include "Game/HeroPawn/Combat/RemoteHeroCombatController.h"
#include "Game/HeroPawn/Abilities/RemoteHeroAbilityController.h"
#include "Game/HeroPawn/Expression/RemoteHeroExpressionController.h"
#include "Multiplayer/Movement/ReplicatedActorMovement.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <cstdint>
#include <memory>
#include <string>

namespace fable::game
{
    class Entity;
    class EntityService;
    class NpcService;
    class ScriptControl;
}

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionService;
}

namespace fable::game::creature::look
{
    class CreatureLookService;
}

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::game::creature::animation
{
    class CreatureAnimationService;
}

namespace fable::game::hero_pawn::abilities
{
    class HeroWillAbilityService;
}

namespace fable::game::hero_pawn::equipment::hooks
{
    class RemoteRangedWeaponOrientationHook;
}

namespace fable::multiplayer::combat
{
    class PlayerCombatantDirectory;
}

namespace fable::multiplayer::entities
{
    class LiveEntityRegistry;
}

namespace fable::game::hero_pawn::remote
{
    class RemoteHeroNativeLifecycle;

    using multiplayer::PlayerState;
    namespace movement = multiplayer::movement;

    enum class RemoteHeroLifecyclePhase : std::uint8_t
    {
        Constructing,
        NativeReady,
        AppearanceApplied,
        Active,
    };

    enum class RemoteHeroActivationResult : std::uint8_t
    {
        Pending,
        Ready,
        Failed,
    };

    // Actor-scoped aggregate for one remote Hero. Networking owns only its
    // actor-ID registry; Hero behavior is composed from Hero-domain
    // appearance, equipment, combat, and actor-generic movement controllers.
    class RemoteHeroActor final
    {
    public:
        RemoteHeroActor();
        ~RemoteHeroActor();

        bool Initialize(
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
                RemoteRangedWeaponOrientationHook& orientationHook);
        void Reconcile(
            const multiplayer::replication::RemotePlayerSnapshot& snapshot,
            const std::string& localMap,
            std::uint16_t localMapId,
            game::Entity* localHero,
            const multiplayer::entities::LiveEntityRegistry* liveEntities =
                nullptr);
        // Compatibility overload for callers that only have a legacy
        // snapshot. New lifecycle-aware callers must use the overload above.
        void Reconcile(
            const PlayerState& state,
            const std::string& localMap,
            game::Entity* localHero,
            std::uint64_t receivedAt);
        void BeginWorldTransition() noexcept;
        void CompleteWorldTransition() noexcept;
        void DriveMovement();
        bool ApplyHealth(
            float currentHealth,
            float maximumHealth,
            std::uint32_t revision);
        bool PerformAbility(
            game::creature::equipment::CreatureWeaponFamily weaponFamily,
            const game::hero_pawn::equipment::HeroWeaponDefinitions&
                requiredWeapons,
            std::uint32_t meleeAttachmentSlot,
            std::uint32_t rangedAttachmentSlot,
            std::uint32_t abilityId,
            float charge,
            void* targetCreature,
            const std::string& resolvedActionType,
            std::uint32_t resolvedAnimationId);
        bool EndRangedAim() noexcept;
        bool PerformWeaponTransition(
            game::creature::equipment::CreatureWeaponFamily weaponFamily,
            const game::hero_pawn::equipment::HeroWeaponDefinitions&
                requiredWeapons,
            std::uint32_t meleeAttachmentSlot,
            std::uint32_t rangedAttachmentSlot,
            const std::string& resolvedActionType,
            std::uint32_t resolvedAnimationId,
            std::uint64_t actionId);
        [[nodiscard]] bool IsWeaponTransitionPending() const noexcept;
        bool PerformHeroAbility(
            game::hero_pawn::abilities::HeroAbility ability,
            game::hero_pawn::abilities::HeroAbilityCommand command,
            std::int32_t progressionState,
            void* targetCreature);
        bool PerformExpression(
            const std::string& expressionDefinition,
            void* targetCreature,
            const std::string& resolvedActionType,
            std::uint32_t resolvedAnimationId,
            std::int32_t expressionDurationTicks,
            std::int32_t expressionTriggerTicks);
        void Shutdown() noexcept;
        [[nodiscard]] bool IsActive() const;
        [[nodiscard]] RemoteHeroLifecyclePhase LifecyclePhase() const noexcept
        {
            return lifecyclePhase_;
        }
        [[nodiscard]] bool IsLifecycleActive() const noexcept
        {
            return lifecyclePhase_ == RemoteHeroLifecyclePhase::Active;
        }
        [[nodiscard]] bool MatchesLifecycle(
            std::uint32_t actorGeneration,
            std::uint32_t mapEpoch) const noexcept;

    private:
        friend class RemoteHeroNativeLifecycle;

        static bool ReadMovement(
            void* context,
            void* creature,
            movement::ReplicatedActorMovement::NativeInput& input);
        static movement::ReplicatedMovementSample MovementSample(
            const PlayerState& state,
            std::uint64_t receivedAt);
        [[nodiscard]] bool IsMovementReady() const;

        game::EntityService* entities_ = nullptr;
        game::NpcService* npcs_ = nullptr;
        game::creature::look::CreatureLookService* look_ = nullptr;
        multiplayer::combat::PlayerCombatantDirectory* combatants_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        game::hero_pawn::appearance::hooks::RemoteHeroPresentationFactoryHook*
            presentationFactory_ = nullptr;
        game::hero_pawn::appearance::hooks::RemoteHeroDefinitionHook*
            definitionHook_ = nullptr;
        movement::ReplicatedActorMovement movement_;
        game::hero_pawn::appearance::RemoteHeroAppearanceController
            appearance_;
        game::hero_pawn::equipment::RemoteHeroEquipmentController equipment_;
        game::hero_pawn::combat::RemoteHeroCombatController combat_;
        game::hero_pawn::abilities::RemoteHeroAbilityController abilities_;
        game::hero_pawn::expression::RemoteHeroExpressionController
            expressions_;
        game::hero_pawn::appearance::hooks::
            RemoteHeroPresentationFactoryHook::ArmToken factoryArmToken_ = 0;
        game::hero_pawn::appearance::hooks::
            RemoteHeroDefinitionHook::ArmToken definitionArmToken_ = 0;
        game::Entity* avatar_ = nullptr;
        void* nativeAvatar_ = nullptr;
        void* nativeCompanionHero_ = nullptr;
        game::ScriptControl* control_ = nullptr;
        std::string playerId_;
        std::string appearanceDefinition_;
        std::uint64_t actorId_ = 0;
        std::uint64_t nextSpawnAttemptAt_ = 0;
        bool initialized_ = false;
        bool presentationStateReported_ = false;
        bool worldTransitionActive_ = false;
        bool separationReported_ = false;
        bool companionRegistered_ = false;
        // Presence is observed asynchronously after Spawn. Only retire a
        // wrapper after it was present once and then disappears.
        bool nativePresenceObserved_ = false;
        std::uint64_t lastPresentationRepairAt_ = 0;
        bool presentationRepairFailureReported_ = false;
        RemoteHeroLifecyclePhase lifecyclePhase_ =
            RemoteHeroLifecyclePhase::Constructing;
        std::uint32_t actorGeneration_ = 0;
        std::uint32_t mapEpoch_ = 0;
        bool appearanceBaselineApplied_ = false;
        bool equipmentBaselineApplied_ = false;
        std::unique_ptr<RemoteHeroNativeLifecycle> lifecycle_;
    };
}
