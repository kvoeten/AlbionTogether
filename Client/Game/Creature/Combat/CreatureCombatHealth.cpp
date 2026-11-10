#include "CreatureCombatService.h"

namespace fable::game::creature::combat
{
    void CreatureCombatService::CaptureHealthMutation(
        void* context,
        const CombatHealthMutationEvent& event) noexcept
    {
        auto* const service = static_cast<CreatureCombatService*>(context);
        if (service == nullptr)
        {
            return;
        }
        std::array<HealthMutationSinkEntry, HealthMutationSinkCapacity>
            sinks = {};
        AcquireSRWLockShared(&service->healthMutationSinkLock_);
        sinks = service->healthMutationSinks_;
        ReleaseSRWLockShared(&service->healthMutationSinkLock_);
        for (const HealthMutationSinkEntry& entry : sinks)
        {
            if (entry.sink != nullptr)
            {
                entry.sink(entry.context, event);
            }
        }
    }

    bool CreatureCombatService::AddHealthMutationSink(
        HealthMutationSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            return false;
        }
        AcquireSRWLockExclusive(&healthMutationSinkLock_);
        for (const HealthMutationSinkEntry& entry : healthMutationSinks_)
        {
            if (entry.sink == sink && entry.context == context)
            {
                ReleaseSRWLockExclusive(&healthMutationSinkLock_);
                return true;
            }
        }
        for (HealthMutationSinkEntry& entry : healthMutationSinks_)
        {
            if (entry.sink == nullptr)
            {
                entry = {sink, context};
                ReleaseSRWLockExclusive(&healthMutationSinkLock_);
                return true;
            }
        }
        ReleaseSRWLockExclusive(&healthMutationSinkLock_);
        return false;
    }

    void CreatureCombatService::RemoveHealthMutationSink(
        HealthMutationSink sink,
        void* context) noexcept
    {
        AcquireSRWLockExclusive(&healthMutationSinkLock_);
        for (HealthMutationSinkEntry& entry : healthMutationSinks_)
        {
            if (entry.sink == sink && entry.context == context)
            {
                entry = {};
            }
        }
        ReleaseSRWLockExclusive(&healthMutationSinkLock_);
    }

    void CreatureCombatService::CaptureResolvedHit(
        void* context,
        const ResolvedHitEvent& event) noexcept
    {
        auto* const service = static_cast<CreatureCombatService*>(context);
        if (service == nullptr)
        {
            return;
        }
        std::array<ResolvedHitSinkEntry, ResolvedHitSinkCapacity> sinks = {};
        AcquireSRWLockShared(&service->resolvedHitSinkLock_);
        sinks = service->resolvedHitSinks_;
        ReleaseSRWLockShared(&service->resolvedHitSinkLock_);
        for (const ResolvedHitSinkEntry& entry : sinks)
        {
            if (entry.sink != nullptr)
            {
                entry.sink(entry.context, event);
            }
        }
    }

    bool CreatureCombatService::AddResolvedHitSink(
        ResolvedHitSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            return false;
        }
        AcquireSRWLockExclusive(&resolvedHitSinkLock_);
        for (const ResolvedHitSinkEntry& entry : resolvedHitSinks_)
        {
            if (entry.sink == sink && entry.context == context)
            {
                ReleaseSRWLockExclusive(&resolvedHitSinkLock_);
                return true;
            }
        }
        for (ResolvedHitSinkEntry& entry : resolvedHitSinks_)
        {
            if (entry.sink == nullptr)
            {
                entry = {sink, context};
                ReleaseSRWLockExclusive(&resolvedHitSinkLock_);
                return true;
            }
        }
        ReleaseSRWLockExclusive(&resolvedHitSinkLock_);
        return false;
    }

    void CreatureCombatService::RemoveResolvedHitSink(
        ResolvedHitSink sink,
        void* context) noexcept
    {
        AcquireSRWLockExclusive(&resolvedHitSinkLock_);
        for (ResolvedHitSinkEntry& entry : resolvedHitSinks_)
        {
            if (entry.sink == sink && entry.context == context)
            {
                entry = {};
            }
        }
        ReleaseSRWLockExclusive(&resolvedHitSinkLock_);
    }

    bool CreatureCombatService::SetReplicaHealthProtection(
        void* creature,
        bool protectedReplica) noexcept
    {
        return combatHealthMutationHook_.SetReplicaProtected(
            creature, protectedReplica);
    }

    bool CreatureCombatService::ReadCombatHealth(
        void* creature,
        float& currentHealth,
        float& maximumHealth) const noexcept
    {
        return combatHealthMutationHook_.Read(
            creature, currentHealth, maximumHealth);
    }

    bool CreatureCombatService::ApplyAuthoritativeCombatHealth(
        void* creature,
        float currentHealth,
        float maximumHealth) noexcept
    {
        return combatHealthMutationHook_.ApplyAuthoritative(
            creature, currentHealth, maximumHealth);
    }

    bool CreatureCombatService::ApplyOwnedCombatDamage(
        void* creature,
        const float damage) noexcept
    {
        return combatHealthMutationHook_.ApplyOwnedCombatDamage(
            creature, damage);
    }

    bool CreatureCombatService::ApplyOwnedCombatHealing(
        void* creature,
        const float healing) noexcept
    {
        return combatHealthMutationHook_.ApplyOwnedCombatHealing(
            creature, healing);
    }
}
