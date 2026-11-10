#include "RemoteHeroEquipmentController.h"

#include "Game/Creature/Equipment/Native/CreatureWeaponFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/HeroPawn/Equipment/Native/HeroWeaponComponentProvisioner.h"

#include <cstdio>

namespace fable::multiplayer::presentation::remote_hero::equipment
{
    namespace creature = ::fable::game::creature;
    namespace native = ::fable::game::hero_pawn::equipment::native;

    bool RemoteHeroEquipmentController::ActiveWeaponPresent(
        const creature::equipment::CreatureWeaponFamily family,
        const creature::equipment::native::CreatureWeaponInspection&
            inspection) noexcept
    {
        return family == creature::equipment::CreatureWeaponFamily::Melee
            ? inspection.meleePresent && inspection.meleeWeapon != nullptr
            : family == creature::equipment::CreatureWeaponFamily::Ranged
                ? inspection.rangedPresent &&
                    inspection.rangedWeapon != nullptr
                : true;
    }

    bool RemoteHeroEquipmentController::Initialize(
        game::EntityService& entities,
        game::creature::animation::CreatureAnimationService& animation,
        hooks::RemoteRangedWeaponOrientationHook& orientationHook,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        entities_ = &entities;
        orientationHook_ = &orientationHook;
        diagnostics_ = diagnostics;
        return transitions_.Initialize(entities, animation, diagnostics);
    }

    bool RemoteHeroEquipmentController::Bind(
        game::Entity& hero,
        void* nativeHero,
        std::uint64_t actorId) noexcept
    {
        Unbind();
        if (entities_ == nullptr || nativeHero == nullptr || actorId == 0)
        {
            return false;
        }
        native::HeroWeaponComponentProvisioning provisioning;
        if (!native::HeroWeaponComponentProvisioner::Ensure(
                entities_->GameModule(), nativeHero, provisioning))
        {
            diagnostics_.Event(
                "MultiplayerRemoteHeroEquipmentBindFailed",
                "reason=hero-weapon-inventory-provisioning");
            return false;
        }
        fable::game::creature::equipment::native::CreatureWeaponInspection
            carrying;
        if (orientationHook_ == nullptr ||
            !creature::equipment::native::CreatureWeaponFunctions::Inspect(
                nativeHero, -1, -1, carrying) ||
            carrying.carryingComponent == nullptr)
        {
            diagnostics_.Event(
                "MultiplayerRemoteHeroEquipmentBindFailed",
                "reason=carrying-orientation-registration");
            return false;
        }
        orientationToken_ = orientationHook_->Register(
            carrying.carryingComponent, actorId);
        if (orientationToken_ == 0)
        {
            diagnostics_.Event(
                "MultiplayerRemoteHeroEquipmentBindFailed",
                "reason=carrying-orientation-capacity");
            return false;
        }
        hero_ = &hero;
        nativeHero_ = nativeHero;
        actorId_ = actorId;
        transitions_.Bind(hero, nativeHero, actorId, weaponCache_);
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu hero=%p hero_core=%p hero_core_added=%s component=%p added=%s",
            static_cast<unsigned long long>(actorId_),
            nativeHero_,
            provisioning.heroCore,
            provisioning.heroCoreAdded ? "true" : "false",
            provisioning.component,
            provisioning.added ? "true" : "false");
        diagnostics_.Event("MultiplayerRemoteHeroEquipmentBound", detail);
        return true;
    }

    void RemoteHeroEquipmentController::Unbind() noexcept
    {
        transitions_.Unbind();
        weaponCache_.Reset();
        if (orientationHook_ != nullptr && orientationToken_ != 0)
        {
            orientationHook_->Unregister(orientationToken_);
        }
        orientationToken_ = 0;
        hero_ = nullptr;
        nativeHero_ = nullptr;
        actorId_ = 0;
        applied_ = {};
        attempted_ = {};
        preparedWeapons_ = {};
        cachedWeapons_ = {};
        actionOverrideWeapons_ = {};
        prunedPresentation_ = {};
        activeFamily_ =
            fable::game::creature::equipment::CreatureWeaponFamily::None;
        actionOverrideFamily_ =
            fable::game::creature::equipment::CreatureWeaponFamily::None;
        actionOverrideMeleeSlot_ = 0;
        actionOverrideRangedSlot_ = 0;
        nextAttemptAt_ = 0;
        nextCacheWarmAt_ = 0;
        actionOverrideUntil_ = 0;
        nextPruneAttemptAt_ = 0;
        lastTransitionActionId_ = 0;
        attemptCount_ = 0;
        pendingReported_ = false;
        activeWeaponReady_ = false;
    }

    bool RemoteHeroEquipmentController::IsReady() const noexcept
    {
        return hero_ != nullptr && hero_->IsValid() && nativeHero_ != nullptr &&
            applied_.IsSane() && activeWeaponReady_;
    }

    bool RemoteHeroEquipmentController::IsTransitionPending() const noexcept
    {
        return transitions_.IsPending();
    }

    void RemoteHeroEquipmentController::Shutdown() noexcept
    {
        Unbind();
        transitions_.Shutdown();
        entities_ = nullptr;
        orientationHook_ = nullptr;
        diagnostics_ = {};
    }
}
