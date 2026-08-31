#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::world::travel::native
{
    struct NativeName final
    {
        std::uint32_t index = 0;
        std::uint32_t number = 0;
    };

    struct NativeNameArray final
    {
        const NativeName* data = nullptr;
        std::int32_t count = 0;
        std::int32_t capacity = 0;
    };

    struct WorldTravelFunctions final
    {
        using RegionExitTriggerPointer = void(__thiscall*)(void* component);
        using ResolveConnectedThingPointer = void* (__thiscall*)(
            void* weakThingReference);
        using PrepareMapChangePointer = void(__thiscall*)(
            void* worldInfo,
            const NativeNameArray* levelNames);

        static constexpr std::uintptr_t RegionExitTriggerRva = 0x019275E0;
        static constexpr std::uintptr_t ResolveConnectedThingRva =
            0x012E6EA0;
        static constexpr std::uintptr_t PrepareMapChangeRva = 0x0078A4A0;
        static constexpr std::uintptr_t PrepareMapChangeExceptionHandlerRva =
            0x0240ADF0;
        static constexpr std::uintptr_t NameEntryTableSlotRva = 0x031071E8;
        static constexpr std::uintptr_t NameEntryCountRva = 0x031071EC;
        static constexpr std::uintptr_t RegionTravelStateSlotRva =
            0x0322F1B0;
        static constexpr std::size_t RegionTravelManagerOffset = 0x30;
        static constexpr std::size_t RegionTravelHeroContainerOffset = 0x3C;
        static constexpr std::size_t RegionTravelRequestStateOffset = 0x110;
        static constexpr std::uintptr_t SelectPlayerCreatureRva =
            0x018FD7D0;
        static constexpr std::uintptr_t ResolveIntelligentThingRva =
            0x018E3AD0;
        static constexpr std::size_t RegionExitDisplacedBytes = 8;
        static constexpr std::size_t PrepareMapChangeDisplacedBytes = 7;

        static bool ResolveRegionExitTrigger(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveConnectedThing(
            HMODULE gameModule,
            ResolveConnectedThingPointer& function) noexcept;
        static bool ReadRegionExitHeroReady(
            HMODULE gameModule,
            bool& ready) noexcept;
        static bool ReadRegionExitHeroState(
            HMODULE gameModule,
            bool& ready,
            std::uint16_t& mapId) noexcept;
        static bool ReadRegionTravelRequestState(
            HMODULE gameModule,
            std::uint32_t& state) noexcept;
        static bool ResolvePrepareMapChange(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveName(
            HMODULE gameModule,
            const NativeName& name,
            char* destination,
            std::size_t destinationCapacity) noexcept;
    };
}
