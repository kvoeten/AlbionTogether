#include "CreatureService.h"

#include "../Entity/Entity.h"
#include "../Entity/EntityService.h"
#include "../Native/Addresses.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
    using ModifyCombatHealth = void(__thiscall*)(void*, float, bool);

    constexpr std::size_t kMaximumHealthOffset = 0xCC;
    constexpr std::size_t kHealthOffset = 0xD0;
}

namespace fable::game
{
    bool CreatureService::Initialize(
        EntityService& entities,
        const core::Diagnostics& diagnostics)
    {
        entities_ = &entities;
        diagnostics_ = diagnostics;
        return entities.GameModule() != nullptr;
    }

    void* CreatureService::ResolveCreature(Entity* entity) const
    {
        if (entities_ == nullptr || entity == nullptr)
        {
            return nullptr;
        }

        void* const thing = entities_->ResolveNative(entity->NativeHandle());
        if (thing == nullptr)
        {
            return nullptr;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(entities_->GameModule());
        bool creature = false;
        __try
        {
            void* const vtable = *reinterpret_cast<void**>(thing);
            creature = vtable == reinterpret_cast<void*>(
                    base + native::rva::ThingCreatureVtable) ||
                vtable == reinterpret_cast<void*>(
                    base + native::rva::ThingPlayerCreatureVtable);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            creature = false;
        }
        return creature ? thing : nullptr;
    }

    bool CreatureService::IsCreature(Entity* entity) const
    {
        return ResolveCreature(entity) != nullptr;
    }

    float CreatureService::GetHealth(Entity* entity) const
    {
        void* const creature = ResolveCreature(entity);
        if (creature == nullptr)
        {
            return -1.0f;
        }

        float health = -1.0f;
        __try
        {
            const auto* bytes = static_cast<const std::uint8_t*>(creature);
            const float value = *reinterpret_cast<const float*>(bytes + kHealthOffset);
            const float maximum = *reinterpret_cast<const float*>(
                bytes + kMaximumHealthOffset);
            if (std::isfinite(value) && std::isfinite(maximum) &&
                maximum > 0.0f && value >= 0.0f && value <= maximum + 0.01f)
            {
                health = value;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            health = -1.0f;
        }
        return health;
    }

    float CreatureService::GetMaximumHealth(Entity* entity) const
    {
        void* const creature = ResolveCreature(entity);
        if (creature == nullptr)
        {
            return -1.0f;
        }

        float maximum = -1.0f;
        __try
        {
            const auto* bytes = static_cast<const std::uint8_t*>(creature);
            const float value = *reinterpret_cast<const float*>(
                bytes + kMaximumHealthOffset);
            if (std::isfinite(value) && value > 0.0f)
            {
                maximum = value;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            maximum = -1.0f;
        }
        return maximum;
    }

    bool CreatureService::SetHealth(Entity* entity, float health)
    {
        void* const creature = ResolveCreature(entity);
        const float current = GetHealth(entity);
        const float maximum = GetMaximumHealth(entity);
        if (creature == nullptr || current < 0.0f || maximum <= 0.0f ||
            !std::isfinite(health))
        {
            return false;
        }

        const float target = std::clamp(health, 0.0f, maximum);
        bool applied = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(creature);
            const auto modify = reinterpret_cast<ModifyCombatHealth>(
                vtable[native::thing_creature_slot::ModifyCombatHealth]);
            modify(creature, target - current, false);
            applied = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }
        if (!applied)
        {
            diagnostics_.Log("Creature API: combat-health mutation failed.");
        }
        return applied;
    }
}
