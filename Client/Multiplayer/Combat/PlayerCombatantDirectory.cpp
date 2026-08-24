#include "PlayerCombatantDirectory.h"

namespace
{
    std::uint64_t ReadThingUid(void* creature) noexcept
    {
        std::uint64_t uid = 0;
        if (creature == nullptr)
        {
            return 0;
        }
        __try
        {
            uid = *reinterpret_cast<const std::uint64_t*>(
                static_cast<const std::uint8_t*>(creature) + 0x14);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            uid = 0;
        }
        return uid;
    }
}

namespace fable::multiplayer::combat
{
    bool PlayerCombatantDirectory::Bind(
        std::uint64_t actorId,
        void* creature) noexcept
    {
        if (actorId == 0 || creature == nullptr)
        {
            return false;
        }
        AcquireSRWLockExclusive(&lock_);
        const auto previousCreature = creaturesByActor_.find(actorId);
        if (previousCreature != creaturesByActor_.end())
        {
            const std::uint64_t previousUid = ReadThingUid(
                previousCreature->second);
            if (previousUid != 0)
            {
                actorsByThingUid_.erase(previousUid);
            }
            actorsByCreature_.erase(previousCreature->second);
        }
        const auto previousActor = actorsByCreature_.find(creature);
        if (previousActor != actorsByCreature_.end() &&
            previousActor->second != actorId)
        {
            creaturesByActor_.erase(previousActor->second);
        }
        creaturesByActor_[actorId] = creature;
        actorsByCreature_[creature] = actorId;
        const std::uint64_t thingUid = ReadThingUid(creature);
        if (thingUid != 0)
        {
            actorsByThingUid_[thingUid] = actorId;
        }
        ReleaseSRWLockExclusive(&lock_);
        return true;
    }

    void PlayerCombatantDirectory::Unbind(
        std::uint64_t actorId,
        void* creature) noexcept
    {
        if (actorId == 0)
        {
            return;
        }
        AcquireSRWLockExclusive(&lock_);
        const auto found = creaturesByActor_.find(actorId);
        if (found != creaturesByActor_.end() &&
            (creature == nullptr || found->second == creature))
        {
            const std::uint64_t thingUid = ReadThingUid(found->second);
            if (thingUid != 0)
            {
                actorsByThingUid_.erase(thingUid);
            }
            actorsByCreature_.erase(found->second);
            creaturesByActor_.erase(found);
        }
        ReleaseSRWLockExclusive(&lock_);
    }

    std::uint64_t PlayerCombatantDirectory::FindActorByThingUid(
        std::uint64_t thingUid) const noexcept
    {
        if (thingUid == 0)
        {
            return 0;
        }
        AcquireSRWLockShared(&lock_);
        const auto found = actorsByThingUid_.find(thingUid);
        const std::uint64_t actorId = found != actorsByThingUid_.end()
            ? found->second
            : 0;
        ReleaseSRWLockShared(&lock_);
        return actorId;
    }

    std::uint64_t PlayerCombatantDirectory::FindActor(
        void* creature) const noexcept
    {
        if (creature == nullptr)
        {
            return 0;
        }
        AcquireSRWLockShared(&lock_);
        const auto found = actorsByCreature_.find(creature);
        const std::uint64_t actorId = found != actorsByCreature_.end()
            ? found->second
            : 0;
        ReleaseSRWLockShared(&lock_);
        return actorId;
    }

    void* PlayerCombatantDirectory::FindCreature(
        std::uint64_t actorId) const noexcept
    {
        if (actorId == 0)
        {
            return nullptr;
        }
        AcquireSRWLockShared(&lock_);
        const auto found = creaturesByActor_.find(actorId);
        void* const creature = found != creaturesByActor_.end()
            ? found->second
            : nullptr;
        ReleaseSRWLockShared(&lock_);
        return creature;
    }

    void PlayerCombatantDirectory::Clear() noexcept
    {
        AcquireSRWLockExclusive(&lock_);
        creaturesByActor_.clear();
        actorsByCreature_.clear();
        actorsByThingUid_.clear();
        ReleaseSRWLockExclusive(&lock_);
    }
}
