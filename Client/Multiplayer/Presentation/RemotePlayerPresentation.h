#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/HeroPawn/Appearance/Hooks/RemoteHeroPresentationFactoryHook.h"
#include "Multiplayer/Movement/ReplicatedActorMovement.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <cstdint>
#include <string>
#include <vector>

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

namespace fable::multiplayer::presentation
{
    // Owns one map-scoped remote player's native actor and appearance. The
    // actor-generic movement consumer is composed here, not implemented here.
    class RemotePlayerPresentation final
    {
    public:
        bool Initialize(
            game::EntityService& entities,
            game::NpcService& npcs,
            game::creature::locomotion::CreatureLocomotionService& locomotion,
            game::creature::look::CreatureLookService& look,
            game::creature::combat::CreatureCombatService& combat,
            const core::Diagnostics& diagnostics,
            game::hero_pawn::appearance::hooks::
                RemoteHeroPresentationFactoryHook& presentationFactory);
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
        void Shutdown() noexcept;
        [[nodiscard]] bool IsActive() const;

    private:
        bool Spawn(
            const PlayerState& state,
            const std::string& localMap,
            std::uint64_t receivedAt);
        void Suspend(
            const PlayerState& state,
            const std::string& localMap) noexcept;
        bool Resume(
            const PlayerState& state,
            const std::string& localMap,
            std::uint64_t receivedAt);
        void Retire(bool worldUnloading = false) noexcept;
        static bool ReadMovement(
            void* context,
            void* creature,
            movement::ReplicatedActorMovement::NativeInput& input);
        static movement::ReplicatedMovementSample MovementSample(
            const PlayerState& state,
            std::uint64_t receivedAt);

        game::EntityService* entities_ = nullptr;
        game::NpcService* npcs_ = nullptr;
        game::creature::look::CreatureLookService* look_ = nullptr;
        game::creature::combat::CreatureCombatService* combat_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        game::hero_pawn::appearance::hooks::RemoteHeroPresentationFactoryHook*
            presentationFactory_ = nullptr;
        movement::ReplicatedActorMovement movement_;
        game::hero_pawn::appearance::hooks::
            RemoteHeroPresentationFactoryHook::ArmToken factoryArmToken_ = 0;
        game::Entity* avatar_ = nullptr;
        void* nativeAvatar_ = nullptr;
        game::ScriptControl* control_ = nullptr;
        struct DeferredWorldPresentation final
        {
            game::Entity* avatar = nullptr;
            void* nativeAvatar = nullptr;
            game::ScriptControl* control = nullptr;
        } deferred_;
        // Promoted AHeroPawn presentations cannot safely be destroyed while
        // Fable's asynchronous graphics jobs may still reference their Hero
        // components. They are hidden and quarantined when their source world
        // unloads; a fresh map-scoped actor is created in the destination.
        std::vector<game::Entity*> quarantinedAvatars_;
        std::string playerId_;
        std::string appearanceDefinition_;
        std::uint64_t actorId_ = 0;
        game::hero_pawn::appearance::HeroMorphState appliedMorph_ = {};
        game::hero_pawn::appearance::HeroClothingState appliedClothing_ = {};
        game::hero_pawn::appearance::HeroBoneScaleState appliedBoneScales_ = {};
        game::hero_pawn::appearance::HeroAppearanceModifierState
            appliedModifiers_ = {};
        std::uint64_t nextSpawnAttemptAt_ = 0;
        bool initialized_ = false;
        bool graphicRuntimeReported_ = false;
        bool avatarSuspended_ = false;
        bool separationReported_ = false;
        void* healthCreature_ = nullptr;
        std::uint32_t appliedHealthRevision_ = 0;
    };
}
