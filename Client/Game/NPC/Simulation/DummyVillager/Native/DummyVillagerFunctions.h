#pragma once

#include "Game/NPC/Simulation/DummyVillager/DummyVillagerState.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::npc::simulation::native
{
    struct DummyVillagerFunctions final
    {
        using UpdatePointer = void(__thiscall*)(void* component);
        using SerializePointer = bool(__thiscall*)(
            void* component,
            void* serializer,
            void* context);

        static constexpr std::uintptr_t VtableRva = 0x02AF428C;
        static constexpr std::size_t MaterializeVtableSlot = 7;
        static constexpr std::size_t ScheduleVtableSlot = 10;
        static constexpr std::uintptr_t MaterializeRva = 0x019854F0;
        static constexpr std::uintptr_t MaterializeSehRva = 0x0252D660;
        static constexpr std::uintptr_t ScheduleRva = 0x01984F20;
        static constexpr std::uintptr_t SerializeRva = 0x01984600;
        static constexpr std::uintptr_t SerializeSehRva = 0x0252D568;
        static constexpr std::size_t SerializeVtableSlot = 1;
        static constexpr std::size_t MaterializeDisplacedBytes = 7;
        static constexpr std::size_t ScheduleDisplacedBytes = 6;
        static constexpr std::size_t SerializeDisplacedBytes = 7;

        static constexpr std::size_t OwnerThingOffset = 0x04;
        static constexpr std::size_t CreatureUidOffset = 0x38;
        static constexpr std::size_t RecreationDayOffset = 0x40;
        static constexpr std::size_t RecreationFrameOffset = 0x44;
        static constexpr std::size_t RespawnableOffset = 0x48;
        static constexpr std::size_t GuardOffset = 0x49;
        static constexpr std::size_t OwnerThingUidOffset = 0x14;

        static bool ResolveMaterialize(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveSchedule(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveSerialize(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool Read(
            void* nativeThing,
            DummyVillagerState& state) noexcept;
        static bool ReadComponent(
            void* component,
            DummyVillagerState& state) noexcept;
        static bool WriteComponent(
            void* component,
            const DummyVillagerState& state) noexcept;
    };
}
