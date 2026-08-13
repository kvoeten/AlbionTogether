#include "CreatureLocomotionService.h"

#include "CreatureLocomotionState.h"
#include "Game/Creature/Locomotion/Hooks/CreatureModeManagerObserver.h"
#include "Game/Creature/Locomotion/Native/LocomotionComponents.h"
#include "Game/Creature/Locomotion/Native/PhysicsMovementFunctions.h"
#include "Game/Creature/Locomotion/Native/PhysicsNavigatorFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace fable::game::creature::locomotion
{
    CreatureLocomotionService::~CreatureLocomotionService()
    {
        ClearHeroShadow();
    }

    bool CreatureLocomotionService::Initialize(
        EntityService& entities,
        const core::Diagnostics& diagnostics)
    {
        entities_ = &entities;
        gameModule_ = entities.GameModule();
        diagnostics_ = diagnostics;
        return gameModule_ != nullptr &&
            worldPositionMirrorHook_.Install(gameModule_, diagnostics_) &&
            frameInputRouterHook_.Install(gameModule_, diagnostics_);
    }

    CreatureLocomotionState* CreatureLocomotionService::Inspect(Entity* entity) const
    {
        if (entities_ == nullptr || gameModule_ == nullptr ||
            entity == nullptr || !entity->IsValid())
        {
            return nullptr;
        }

        void* const nativeThing = entities_->ResolveNative(entity->NativeHandle());
        if (nativeThing == nullptr)
        {
            return nullptr;
        }

        native::LocomotionComponentSnapshot nativeSnapshot;
        native::LocomotionComponentDefinition::Inspect(
            gameModule_,
            nativeThing,
            nativeSnapshot);

        auto* const state = new CreatureLocomotionState();
        state->valid_ = nativeSnapshot.valid;
        state->hasPhysicsNavigator_ = nativeSnapshot.physicsNavigatorValidated;
        state->hasCreatureNavigation_ = nativeSnapshot.creatureNavigationValidated;
        state->hasAnimationComplex_ = nativeSnapshot.animationComplexValidated;
        state->navigationSolutionCached_ = nativeSnapshot.navigationSolutionCached;
        state->componentCount_ = nativeSnapshot.componentCount;
        state->animationStateHash_ = nativeSnapshot.animationStateHash;
        state->physicsPosition_ = nativeSnapshot.physicsPosition;
        return state;
    }

    bool CreatureLocomotionService::MirrorPhysicsWorldPosition(
        Entity* source,
        Entity* target)
    {
        if (entities_ == nullptr || source == nullptr || target == nullptr ||
            !source->IsValid() || !target->IsValid())
        {
            return false;
        }

        void* const sourceThing = entities_->ResolveNative(source->NativeHandle());
        void* const targetThing = entities_->ResolveNative(target->NativeHandle());
        void* sourcePhysics = nullptr;
        void* targetPhysics = nullptr;
        sourcePhysics = entity::native::ThingComponentAccess::Find(
            sourceThing,
            entity::native::ThingComponentType::PhysicsNavigator);
        targetPhysics = entity::native::ThingComponentAccess::Find(
            targetThing,
            entity::native::ThingComponentType::PhysicsNavigator);
        if (sourcePhysics == nullptr || targetPhysics == nullptr)
        {
            diagnostics_.Log(
                "Movement: source or target has no runtime component type 0x2.");
            return false;
        }
        return worldPositionMirrorHook_.Bind(sourcePhysics, targetPhysics);
    }

    void CreatureLocomotionService::ClearPhysicsWorldPositionMirror() noexcept
    {
        worldPositionMirrorHook_.Clear();
    }

    bool CreatureLocomotionService::MirrorAnimationMotion(
        Entity* source,
        Entity* target)
    {
        if (entities_ == nullptr || source == nullptr || target == nullptr ||
            !source->IsValid() || !target->IsValid())
        {
            return false;
        }
        return CreatureModeManagerObserver::BindAnimationMotionSource(
            entities_->ResolveNative(source->NativeHandle()),
            entities_->ResolveNative(target->NativeHandle()));
    }

    void CreatureLocomotionService::ClearAnimationMotionMirror() noexcept
    {
        CreatureModeManagerObserver::ClearAnimationMotionSource();
    }

    bool CreatureLocomotionService::RoutePlayerFrameInput(
        Entity* source,
        Entity* target)
    {
        if (entities_ == nullptr || source == nullptr || target == nullptr ||
            !source->IsValid() || !target->IsValid())
        {
            return false;
        }

        void* const targetThing = entities_->ResolveNative(target->NativeHandle());
        void* const targetNavigator = entity::native::ThingComponentAccess::Find(
            targetThing,
            entity::native::ThingComponentType::PhysicsNavigator);
        return targetNavigator != nullptr && frameInputRouterHook_.Bind(
            entities_->ResolveNative(source->NativeHandle()),
            targetNavigator);
    }

    void CreatureLocomotionService::ClearPlayerFrameInputRouter() noexcept
    {
        frameInputRouterHook_.Clear();
    }

    void CreatureLocomotionService::SetPlayerFrameObserver(
        PlayerFrameObserver observer,
        void* context) noexcept
    {
        frameInputRouterHook_.SetFrameObserver(observer, context);
    }

    bool CreatureLocomotionService::RouteHeroShadow(
        Entity* sourcePuppet,
        Entity* targetHero)
    {
        if (entities_ == nullptr || sourcePuppet == nullptr || targetHero == nullptr ||
            sourcePuppet == targetHero || !sourcePuppet->IsValid() ||
            !targetHero->IsValid())
        {
            return false;
        }

        void* const sourceThing = entities_->ResolveNative(
            sourcePuppet->NativeHandle());
        void* const targetThing = entities_->ResolveNative(
            targetHero->NativeHandle());
        void* const sourceNavigator = entity::native::ThingComponentAccess::Find(
            sourceThing,
            entity::native::ThingComponentType::PhysicsNavigator);
        void* const targetControlled = entity::native::ThingComponentAccess::Find(
            targetThing,
            entity::native::ThingComponentType::PhysicsNavigator);
        if (!native::PhysicsWorldPositionFunctions::ValidateNavigatorComponent(
                gameModule_,
                sourceNavigator) ||
            !native::PhysicsWorldPositionFunctions::ValidateControlledComponent(
                gameModule_,
                targetControlled))
        {
            return false;
        }

        ClearHeroShadow();
        sourcePuppet->AddRef();
        targetHero->AddRef();
        retainedShadowSource_ = sourcePuppet;
        retainedShadowTarget_ = targetHero;
        shadowSourceNavigator_ = sourceNavigator;
        shadowTargetControlled_ = targetControlled;
        heroShadowUpdateCount_ = 0;

        char detail[224] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "source_puppet=%p source_navigator=%p target_hero=%p target_controlled=%p",
            sourceThing,
            sourceNavigator,
            targetThing,
            targetControlled);
        diagnostics_.Event("HeroShadowFollowBound", detail);
        return true;
    }

    void CreatureLocomotionService::ClearHeroShadow() noexcept
    {
        shadowSourceNavigator_ = nullptr;
        shadowTargetControlled_ = nullptr;
        if (retainedShadowSource_ != nullptr)
        {
            retainedShadowSource_->Release();
            retainedShadowSource_ = nullptr;
        }
        if (retainedShadowTarget_ != nullptr)
        {
            retainedShadowTarget_->Release();
            retainedShadowTarget_ = nullptr;
        }
        heroShadowUpdateCount_ = 0;
    }

    void CreatureLocomotionService::TickHeroShadow()
    {
        if (shadowSourceNavigator_ == nullptr || shadowTargetControlled_ == nullptr ||
            retainedShadowSource_ == nullptr || retainedShadowTarget_ == nullptr ||
            !retainedShadowSource_->IsValid() || !retainedShadowTarget_->IsValid())
        {
            return;
        }

        Vector3 sourcePosition = {};
        bool readable = false;
        __try
        {
            std::memcpy(
                &sourcePosition,
                static_cast<const unsigned char*>(shadowSourceNavigator_) +
                    native::PhysicsNavigatorFunctions::WorldPositionOffset,
                sizeof(sourcePosition));
            readable = std::isfinite(sourcePosition.x) &&
                std::isfinite(sourcePosition.y) &&
                std::isfinite(sourcePosition.z);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }
        if (!readable ||
            !native::PhysicsWorldPositionFunctions::SetControlledWorldPosition(
                gameModule_,
                shadowTargetControlled_,
                sourcePosition))
        {
            return;
        }

        ++heroShadowUpdateCount_;
        if (heroShadowUpdateCount_ == 1 || heroShadowUpdateCount_ == 10 ||
            heroShadowUpdateCount_ == 60)
        {
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "ordinal=%u position=(%.3f,%.3f,%.3f) source_navigator=%p target_controlled=%p",
                heroShadowUpdateCount_,
                sourcePosition.x,
                sourcePosition.y,
                sourcePosition.z,
                shadowSourceNavigator_,
                shadowTargetControlled_);
            diagnostics_.Event("HeroShadowFollowUpdated", detail);
        }
    }

    bool CreatureLocomotionService::SetPhysicsWorldPosition(
        Entity* entity,
        const Vector3& worldPosition) const
    {
        if (entities_ == nullptr || entity == nullptr || !entity->IsValid())
        {
            return false;
        }
        void* const nativeThing = entities_->ResolveNative(entity->NativeHandle());
        void* physics = nullptr;
        physics = entity::native::ThingComponentAccess::Find(
            nativeThing,
            entity::native::ThingComponentType::PhysicsNavigator);
        return physics != nullptr &&
            native::PhysicsWorldPositionFunctions::SetNavigatorWorldPosition(
                gameModule_,
                physics,
                worldPosition);
    }

    unsigned int CreatureLocomotionService::MirroredPhysicsWorldPositionCount() const noexcept
    {
        return worldPositionMirrorHook_.MirrorCount();
    }

    unsigned int CreatureLocomotionService::MirroredAnimationMotionCount() const noexcept
    {
        return CreatureModeManagerObserver::MirroredAnimationMotionCount();
    }

    unsigned int CreatureLocomotionService::RoutedPlayerFrameCount() const noexcept
    {
        return frameInputRouterHook_.RoutedFrameCount();
    }

    unsigned int CreatureLocomotionService::HeroShadowUpdateCount() const noexcept
    {
        return heroShadowUpdateCount_;
    }
}
