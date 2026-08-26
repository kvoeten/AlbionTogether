#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Equipment/CreatureWeaponFamily.h"
#include "Game/HeroPawn/Combat/RemoteHeroRangedAimController.h"
#include "Game/HeroPawn/Equipment/HeroEquipmentState.h"

#include <cstdint>
#include <string>

namespace fable::game
{
    class Entity;
    class EntityService;
}

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::game::hero_pawn::equipment
{
    class RemoteHeroEquipmentController;
}

namespace fable::game::hero_pawn::combat
{
    // Actor-scoped remote Hero combat bridge. It owns replica health fencing,
    // target assignment, and retail native ability submission.
    class RemoteHeroCombatController final
    {
    public:
        bool Initialize(
            game::EntityService& entities,
            game::creature::combat::CreatureCombatService& combat,
            game::hero_pawn::equipment::RemoteHeroEquipmentController&
                equipment,
            const core::Diagnostics& diagnostics) noexcept;
        [[nodiscard]] bool Bind(
            game::Entity& hero,
            void* nativeHero,
            std::uint64_t actorId) noexcept;
        [[nodiscard]] bool ApplyHealth(
            float currentHealth,
            float maximumHealth,
            std::uint32_t revision);
        [[nodiscard]] bool PerformAbility(
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
        [[nodiscard]] bool EndRangedAim() noexcept;
        void Unbind() noexcept;
        void Shutdown() noexcept;

    private:
        game::EntityService* entities_ = nullptr;
        game::creature::combat::CreatureCombatService* combat_ = nullptr;
        game::hero_pawn::equipment::RemoteHeroEquipmentController*
            equipment_ = nullptr;
        game::Entity* hero_ = nullptr;
        void* nativeHero_ = nullptr;
        std::uint64_t actorId_ = 0;
        void* healthCreature_ = nullptr;
        std::uint32_t appliedHealthRevision_ = 0;
        bool terminalHealthObserved_ = false;
        bool healthReplicaProtected_ = false;
        RemoteHeroRangedAimController rangedAim_;
        core::Diagnostics diagnostics_ = {};
    };
}
