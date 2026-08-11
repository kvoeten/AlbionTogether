#include "Entity.h"

#include "EntityService.h"
#include "Game/Creature/Control/ScriptControl.h"

namespace fable::game
{
    Entity::Entity(EntityService& service, native::ScriptThing handle)
        : service_(&service), handle_(handle)
    {
    }

    Entity::~Entity()
    {
        if (service_ != nullptr)
        {
            service_->ReleaseHandle(handle_);
        }
    }

    void Entity::AddRef() noexcept
    {
        referenceCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void Entity::Release() noexcept
    {
        if (referenceCount_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            delete this;
        }
    }

    bool Entity::IsValid() const
    {
        return service_ != nullptr && service_->IsValid(handle_);
    }

    bool Entity::IsAlive() const
    {
        return service_ != nullptr && service_->IsAlive(handle_);
    }

    bool Entity::IsDead() const
    {
        return service_ == nullptr || service_->IsDead(handle_);
    }

    bool Entity::IsSneaking() const { return service_ != nullptr && service_->IsSneaking(handle_); }
    bool Entity::IsAwareOfHero() const { return service_ != nullptr && service_->IsAwareOfHero(handle_); }
    bool Entity::IsUnconscious() const { return service_ != nullptr && service_->IsUnconscious(handle_); }
    bool Entity::IsUsable() const { return service_ != nullptr && service_->IsUsable(handle_); }
    bool Entity::IsOpenDoor() const { return service_ != nullptr && service_->IsOpenDoor(handle_); }
    bool Entity::IsSummonedCreature() const { return service_ != nullptr && service_->IsSummonedCreature(handle_); }
    std::string Entity::GetName() const { return service_ != nullptr ? service_->GetName(handle_) : std::string{}; }
    std::string Entity::GetDefinitionName() const { return service_ != nullptr ? service_->GetDefinitionName(handle_) : std::string{}; }
    std::string Entity::GetDataString() const { return service_ != nullptr ? service_->GetDataString(handle_) : std::string{}; }
    std::string Entity::GetCurrentMapName() const { return service_ != nullptr ? service_->GetCurrentMapName(handle_) : std::string{}; }
    std::string Entity::GetHomeMapName() const { return service_ != nullptr ? service_->GetHomeMapName(handle_) : std::string{}; }
    bool Entity::GetActivationTriggerStatus() const { return service_ != nullptr && service_->GetActivationTriggerStatus(handle_); }
    int Entity::GetScriptCounter() const { return service_ != nullptr ? service_->GetScriptCounter(handle_) : 0; }

    Vector3 Entity::GetPosition() const
    {
        Vector3 position;
        if (service_ != nullptr)
        {
            service_->ReadPosition(handle_, position);
        }
        return position;
    }

    float Entity::GetFacing() const
    {
        float facing = 0.0f;
        if (service_ != nullptr)
        {
            service_->ReadFacing(handle_, facing);
        }
        return facing;
    }

    bool Entity::Teleport(const Vector3& position, float facing, bool effect)
    {
        return service_ != nullptr && service_->Teleport(handle_, position, facing, effect);
    }

    bool Entity::SetAttackable(bool enabled)
    {
        return service_ != nullptr && service_->SetAttackable(handle_, enabled);
    }

    bool Entity::SetDamageable(bool enabled)
    {
        return service_ != nullptr && service_->SetDamageable(handle_, enabled);
    }

    bool Entity::SetCollidable(bool enabled)
    {
        return service_ != nullptr && service_->SetCollidable(handle_, enabled);
    }

    bool Entity::SetDrawable(bool enabled)
    {
        return service_ != nullptr && service_->SetDrawable(handle_, enabled);
    }

    bool Entity::SetDataString(const std::string& value)
    {
        return service_ != nullptr && service_->SetDataString(handle_, value);
    }

    bool Entity::SetUsable(bool enabled) { return service_ != nullptr && service_->SetUsable(handle_, enabled); }
    bool Entity::SetFriendsWithEverything(bool enabled) { return service_ != nullptr && service_->SetFriendsWithEverything(handle_, enabled); }
    bool Entity::SetActivationTriggerStatus(bool enabled) { return service_ != nullptr && service_->SetActivationTriggerStatus(handle_, enabled); }
    bool Entity::SetKillOnLevelUnload(bool enabled) { return service_ != nullptr && service_->SetKillOnLevelUnload(handle_, enabled); }
    bool Entity::UpdateAttachment() { return service_ != nullptr && service_->UpdateAttachment(handle_); }
    bool Entity::IncrementScriptCounter() { return service_ != nullptr && service_->IncrementScriptCounter(handle_); }
    bool Entity::DecrementScriptCounter() { return service_ != nullptr && service_->DecrementScriptCounter(handle_); }

    bool Entity::Attack(Entity* target, bool stopCurrentAction, bool unsheathe)
    {
        return service_ != nullptr && target != nullptr &&
            service_->Attack(handle_, target->NativeHandle(), stopCurrentAction, unsheathe);
    }

    ScriptControl* Entity::AcquireControl(AiPriority priority)
    {
        return service_ != nullptr
            ? service_->AcquireControl(handle_, priority)
            : nullptr;
    }

    const native::ScriptThing& Entity::NativeHandle() const noexcept
    {
        return handle_;
    }
}
