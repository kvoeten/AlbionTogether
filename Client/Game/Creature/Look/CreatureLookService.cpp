#include "CreatureLookService.h"

#include "Game/Creature/Look/Native/CreatureLookFunctions.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"

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

        ClearMovementFacing();
        if (!native::CreatureLookFunctions::ForceLookAtNothing(
                entities_->Interface(),
                target->NativeHandle()))
        {
            return false;
        }
        if (!facingRouterHook_.Bind(targetThing, targetNavigator))
        {
            native::CreatureLookFunctions::ResetForceLookAt(
                entities_->Interface(),
                target->NativeHandle());
            return false;
        }

        target->AddRef();
        retainedTarget_ = target;
        diagnostics_.Event(
            "CreatureAutonomousLookSuppressed",
            "component 0x08 forced to look at nothing while movement-facing routing is active");
        return true;
    }

    void CreatureLookService::ClearMovementFacing() noexcept
    {
        facingRouterHook_.Clear();
        if (retainedTarget_ == nullptr)
        {
            return;
        }
        if (entities_ != nullptr && retainedTarget_->IsValid())
        {
            native::CreatureLookFunctions::ResetForceLookAt(
                entities_->Interface(),
                retainedTarget_->NativeHandle());
        }
        retainedTarget_->Release();
        retainedTarget_ = nullptr;
    }

    unsigned int CreatureLookService::RoutedMovementFacingCount() const noexcept
    {
        return facingRouterHook_.RoutedFacingCount();
    }
}
