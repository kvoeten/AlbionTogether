#include "DummyVillagerFunctions.h"

#include "Game/Entity/Native/ThingComponentAccess.h"

#include <array>
#include <cstring>

namespace
{
    bool ResolveVtableMethod(
        HMODULE module,
        std::uintptr_t functionRva,
        std::size_t slot,
        const std::uint8_t* prefix,
        std::size_t prefixSize,
        std::uintptr_t sehRva,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (module == nullptr || prefix == nullptr || prefixSize == 0)
        {
            return false;
        }
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(module) + functionRva);
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(
            reinterpret_cast<std::uintptr_t>(module) +
            fable::game::npc::simulation::native::DummyVillagerFunctions::
                VtableRva);
        bool valid = false;
        __try
        {
            valid = std::memcmp(candidate, prefix, prefixSize) == 0 &&
                vtable[slot] == reinterpret_cast<std::uintptr_t>(candidate);
            if (valid && sehRva != 0)
            {
                valid = *reinterpret_cast<const std::uintptr_t*>(
                    candidate + prefixSize) ==
                        reinterpret_cast<std::uintptr_t>(module) + sehRva;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (valid)
        {
            address = candidate;
        }
        return valid;
    }
}

namespace fable::game::npc::simulation::native
{
    bool DummyVillagerFunctions::ResolveMaterialize(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        constexpr std::array<std::uint8_t, 3> prefix = {0x6A, 0xFF, 0x68};
        return ResolveVtableMethod(
            gameModule,
            MaterializeRva,
            MaterializeVtableSlot,
            prefix.data(),
            prefix.size(),
            MaterializeSehRva,
            address);
    }

    bool DummyVillagerFunctions::ResolveSchedule(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        constexpr std::array<std::uint8_t, 6> prefix = {
            0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8,
        };
        return ResolveVtableMethod(
            gameModule,
            ScheduleRva,
            ScheduleVtableSlot,
            prefix.data(),
            prefix.size(),
            0,
            address);
    }

    bool DummyVillagerFunctions::ResolveSerialize(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        constexpr std::array<std::uint8_t, 3> prefix = {0x6A, 0xFF, 0x68};
        return ResolveVtableMethod(
            gameModule,
            SerializeRva,
            SerializeVtableSlot,
            prefix.data(),
            prefix.size(),
            SerializeSehRva,
            address);
    }

    bool DummyVillagerFunctions::Read(
        void* nativeThing,
        DummyVillagerState& state) noexcept
    {
        state = {};
        void* const component = entity::native::ThingComponentAccess::Find(
            nativeThing,
            entity::native::ThingComponentType::DummyVillager);
        if (component == nullptr)
        {
            return true;
        }
        return ReadComponent(component, state);
    }

    bool DummyVillagerFunctions::ReadComponent(
        void* component,
        DummyVillagerState& state) noexcept
    {
        state = {};
        if (component == nullptr)
        {
            return false;
        }
        bool read = false;
        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(
                component);
            state.recreationDay = *reinterpret_cast<const std::int32_t*>(
                bytes + RecreationDayOffset);
            state.recreationFrame = *reinterpret_cast<const std::int32_t*>(
                bytes + RecreationFrameOffset);
            state.respawnable = bytes[RespawnableOffset] != 0;
            state.guard = bytes[GuardOffset] != 0;
            state.componentPresent = true;
            read = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            state = {};
            read = false;
        }
        return read;
    }

    bool DummyVillagerFunctions::WriteComponent(
        void* component,
        const DummyVillagerState& state) noexcept
    {
        if (component == nullptr || !state.componentPresent)
        {
            return false;
        }
        bool written = false;
        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(component);
            *reinterpret_cast<std::int32_t*>(bytes + RecreationDayOffset) =
                state.recreationDay;
            *reinterpret_cast<std::int32_t*>(bytes + RecreationFrameOffset) =
                state.recreationFrame;
            bytes[RespawnableOffset] = state.respawnable ? 1 : 0;
            bytes[GuardOffset] = state.guard ? 1 : 0;
            written = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            written = false;
        }
        return written;
    }
}
