#include "CreatureLookService.h"

#include "Game/Creature/Look/Native/CreatureLookFunctions.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"

#include <algorithm>

namespace fable::game::creature::look
{
    CreatureLookService::~CreatureLookService()
    {
        ClearMovementFacing();
    }

    bool CreatureLookService::Initialize(
        EntityService& entities,
        const core::Diagnostics& diagnostics)
    {
        entities_ = &entities;
        gameModule_ = entities.GameModule();
        diagnostics_ = diagnostics;
        return gameModule_ != nullptr &&
            facingRouterHook_.Install(gameModule_, diagnostics_);
    }

    bool CreatureLookService::RouteMovementFacing(Entity* target)
    {
        return Route(target, nullptr, nullptr);
    }

    bool CreatureLookService::RouteReplicatedMovement(
        Entity* target,
        ReplicatedMovementProvider provider,
        void* context)
    {
        return provider != nullptr && Route(target, provider, context);
    }

    bool CreatureLookService::RouteReplicatedNativeMovement(
        void* nativeTarget,
        ReplicatedMovementProvider provider,
        void* context)
    {
        if (provider == nullptr || nativeTarget == nullptr ||
            !::fable::game::creature::native::CreatureFrameFunctions::
                ValidateCreature(gameModule_, nativeTarget))
        {
            return false;
        }
        void* const navigator = entity::native::ThingComponentAccess::Find(
            nativeTarget,
            entity::native::ThingComponentType::PhysicsNavigator);
        return navigator != nullptr && facingRouterHook_.Bind(
            nativeTarget,
            navigator,
            provider,
            context);
    }

    bool CreatureLookService::Route(
        Entity* target,
        ReplicatedMovementProvider provider,
        void* context)
    {
        if (entities_ == nullptr || target == nullptr || !target->IsValid())
        {
            return false;
        }

        void* const targetThing = entities_->ResolveNative(target->NativeHandle());
        void* const targetNavigator = entity::native::ThingComponentAccess::Find(
            targetThing,
            entity::native::ThingComponentType::PhysicsNavigator);
        if (!::fable::game::creature::native::CreatureFrameFunctions::ValidateCreature(
                gameModule_,
                targetThing) ||
            targetNavigator == nullptr)
        {
            return false;
        }

        const auto existing = std::find_if(
            retainedTargets_.begin(),
            retainedTargets_.end(),
            [target](const RoutedTarget& retained)
            {
                return retained.entity == target;
            });
        if (existing != retainedTargets_.end())
        {
            return facingRouterHook_.Bind(
                targetThing,
                targetNavigator,
                provider,
                context);
        }
        const bool autonomousLookSuppressed =
            native::CreatureLookFunctions::ForceLookAtNothing(
                entities_->Interface(),
                target->NativeHandle());
        if (!facingRouterHook_.Bind(
            targetThing,
            targetNavigator,
            provider,
            context))
        {
            if (autonomousLookSuppressed)
            {
                native::CreatureLookFunctions::ResetForceLookAt(
                    entities_->Interface(),
                    target->NativeHandle());
            }
            return false;
        }

        target->AddRef();
        retainedTargets_.push_back({target, targetThing});
        diagnostics_.Event(
            autonomousLookSuppressed
                ? "CreatureAutonomousLookSuppressed"
                : "CreatureAutonomousLookNotApplicable",
            autonomousLookSuppressed
                ? "component 0x08 forced to look at nothing while movement-facing routing is active"
                : "native movement-facing routing is active without an autonomous look target to suppress");
        return true;
    }

    void CreatureLookService::ClearMovementFacing() noexcept
    {
        facingRouterHook_.Clear();
        for (const RoutedTarget& retained : retainedTargets_)
        {
            Entity* const target = retained.entity;
            if (target == nullptr)
            {
                continue;
            }
            if (entities_ != nullptr && target->IsValid())
            {
                native::CreatureLookFunctions::ResetForceLookAt(
                    entities_->Interface(),
                    target->NativeHandle());
            }
            target->Release();
        }
        retainedTargets_.clear();
    }

    void CreatureLookService::StopRouting(
        Entity* target,
        bool restoreAutonomousLook) noexcept
    {
        const auto existing = std::find_if(
            retainedTargets_.begin(),
            retainedTargets_.end(),
            [target](const RoutedTarget& retained)
            {
                return retained.entity == target;
            });
        if (existing == retainedTargets_.end())
        {
            return;
        }
        facingRouterHook_.Unbind(existing->nativeThing);
        if (restoreAutonomousLook && entities_ != nullptr && target != nullptr &&
            target->IsValid())
        {
            native::CreatureLookFunctions::ResetForceLookAt(
                entities_->Interface(),
                target->NativeHandle());
        }
        if (target != nullptr)
        {
            target->Release();
        }
        retainedTargets_.erase(existing);
    }

    void CreatureLookService::StopRoutingNative(void* nativeTarget) noexcept
    {
        facingRouterHook_.Unbind(nativeTarget);
    }

    bool CreatureLookService::DriveReplicatedMovement(Entity* target)
    {
        const auto existing = std::find_if(
            retainedTargets_.begin(),
            retainedTargets_.end(),
            [target](const RoutedTarget& retained)
            {
                return retained.entity == target;
            });
        return existing != retainedTargets_.end() &&
            facingRouterHook_.Drive(existing->nativeThing);
    }

    bool CreatureLookService::DriveReplicatedNativeMovement(
        void* nativeTarget)
    {
        return facingRouterHook_.Drive(nativeTarget);
    }

    void CreatureLookService::SetCreatureFrameObserver(
        CreatureFrameObserver observer,
        void* context) noexcept
    {
        facingRouterHook_.SetFrameObserver(observer, context);
    }

    unsigned int CreatureLookService::RoutedMovementFacingCount() const noexcept
    {
        return facingRouterHook_.RoutedFacingCount();
    }

    HMODULE CreatureLookService::GameModule() const noexcept
    {
        return gameModule_;
    }
}
