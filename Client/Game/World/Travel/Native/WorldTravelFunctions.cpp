#include "WorldTravelFunctions.h"

#include <cstdio>
#include <cstring>

namespace
{
    constexpr std::uint32_t MaximumNameEntries = 2'000'000;

    bool AppendNumberSuffix(
        std::uint32_t number,
        char* destination,
        std::size_t destinationCapacity,
        std::size_t& length) noexcept
    {
        if (number == 0)
        {
            return true;
        }
        if (length >= destinationCapacity)
        {
            return false;
        }
        const int written = std::snprintf(
            destination + length,
            destinationCapacity - length,
            "_%u",
            number - 1);
        if (written <= 0 ||
            static_cast<std::size_t>(written) >=
                destinationCapacity - length)
        {
            return false;
        }
        length += static_cast<std::size_t>(written);
        return true;
    }
}

namespace fable::game::world::travel::native
{
    bool WorldTravelFunctions::ResolveRegionExitTrigger(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(gameModule) +
            RegionExitTriggerRva);
        bool valid = false;
        __try
        {
            valid = candidate[0] == 0x83 && candidate[1] == 0xEC &&
                candidate[2] == 0x34 && candidate[3] == 0xA1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            return false;
        }
        address = candidate;
        return true;
    }

    bool WorldTravelFunctions::ResolveConnectedThing(
        HMODULE gameModule,
        ResolveConnectedThingPointer& function) noexcept
    {
        function = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        const auto* const candidate = reinterpret_cast<const std::uint8_t*>(
            base + ResolveConnectedThingRva);
        constexpr std::uint8_t expected[] = {
            0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04, 0x85, 0xC0};
        bool valid = false;
        __try
        {
            valid = std::memcmp(candidate, expected, sizeof(expected)) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            return false;
        }
        function = reinterpret_cast<ResolveConnectedThingPointer>(
            base + ResolveConnectedThingRva);
        return true;
    }

    bool WorldTravelFunctions::ReadRegionExitHeroReady(
        HMODULE gameModule,
        bool& ready) noexcept
    {
        std::uint16_t mapId = 0;
        return ReadRegionExitHeroState(gameModule, ready, mapId);
    }

    bool WorldTravelFunctions::ReadRegionExitHeroState(
        HMODULE gameModule,
        bool& ready,
        std::uint16_t& mapId) noexcept
    {
        ready = false;
        mapId = 0;
        if (gameModule == nullptr)
        {
            return false;
        }
        bool readable = false;
        __try
        {
            const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
            void* const state = *reinterpret_cast<void**>(
                base + RegionTravelStateSlotRva);
            void* const container = state == nullptr
                ? nullptr
                : *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(state) +
                    RegionTravelHeroContainerOffset);
            const auto* const selectBytes = reinterpret_cast<const std::uint8_t*>(
                base + SelectPlayerCreatureRva);
            const auto* const resolveBytes = reinterpret_cast<const std::uint8_t*>(
                base + ResolveIntelligentThingRva);
            if (container != nullptr &&
                selectBytes[0] == 0x8B && selectBytes[1] == 0x41 &&
                selectBytes[2] == 0x10 &&
                resolveBytes[0] == 0x83 && resolveBytes[1] == 0xC1 &&
                resolveBytes[2] == 0x2C)
            {
                using SelectPlayerCreature = void* (__thiscall*)(void*);
                using ResolveIntelligentThing = void* (__thiscall*)(void*);
                void* const selected = reinterpret_cast<SelectPlayerCreature>(
                    base + SelectPlayerCreatureRva)(container);
                void* const hero = selected == nullptr
                    ? nullptr
                    : reinterpret_cast<ResolveIntelligentThing>(
                        base + ResolveIntelligentThingRva)(selected);
                if (hero != nullptr)
                {
                    void** const vtable = *reinterpret_cast<void***>(hero);
                    using IsUnavailable = bool(__thiscall*)(void*);
                    mapId = *reinterpret_cast<const std::uint16_t*>(
                        static_cast<const std::uint8_t*>(hero) + 0x9A);
                    ready = vtable != nullptr &&
                        !reinterpret_cast<IsUnavailable>(vtable[4])(hero);
                }
                readable = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ready = false;
            mapId = 0;
            readable = false;
        }
        return readable;
    }

    bool WorldTravelFunctions::ReadRegionTravelRequestState(
        HMODULE gameModule,
        std::uint32_t& state) noexcept
    {
        state = 0;
        if (gameModule == nullptr)
        {
            return false;
        }
        bool readable = false;
        __try
        {
            const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
            void* const world = *reinterpret_cast<void**>(
                base + RegionTravelStateSlotRva);
            void* const manager = world == nullptr
                ? nullptr
                : *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(world) +
                    RegionTravelManagerOffset);
            if (manager != nullptr)
            {
                state = *reinterpret_cast<const std::uint32_t*>(
                    static_cast<const std::uint8_t*>(manager) +
                    RegionTravelRequestStateOffset);
                readable = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            state = 0;
            readable = false;
        }
        return readable;
    }

    bool WorldTravelFunctions::ResolvePrepareMapChange(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            base + PrepareMapChangeRva);
        std::uintptr_t exceptionHandler = 0;
        bool valid = false;
        __try
        {
            valid = candidate[0] == 0x6A && candidate[1] == 0xFF &&
                candidate[2] == 0x68 && candidate[7] == 0x64 &&
                candidate[8] == 0xA1;
            std::memcpy(
                &exceptionHandler,
                candidate + 3,
                sizeof(exceptionHandler));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid ||
            exceptionHandler != base + PrepareMapChangeExceptionHandlerRva)
        {
            return false;
        }
        address = candidate;
        return true;
    }

    bool WorldTravelFunctions::ResolveName(
        HMODULE gameModule,
        const NativeName& name,
        char* destination,
        std::size_t destinationCapacity) noexcept
    {
        if (destination == nullptr || destinationCapacity == 0)
        {
            return false;
        }
        destination[0] = '\0';
        if (gameModule == nullptr)
        {
            return false;
        }

        bool valid = false;
        __try
        {
            const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
            auto* const entries = *reinterpret_cast<void***>(
                base + NameEntryTableSlotRva);
            const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(
                base + NameEntryCountRva);
            if (entries == nullptr || count == 0 ||
                count > MaximumNameEntries || name.index >= count)
            {
                return false;
            }
            const auto* const entry = static_cast<const std::uint8_t*>(
                entries[name.index]);
            if (entry == nullptr)
            {
                return false;
            }
            const bool wide = (entry[8] & 0x01u) != 0;
            std::size_t length = 0;
            if (wide)
            {
                const auto* const text = reinterpret_cast<const wchar_t*>(
                    entry + 0x10);
                while (length + 1 < destinationCapacity &&
                    text[length] != L'\0')
                {
                    const wchar_t character = text[length];
                    destination[length] = character >= 0x20 &&
                            character <= 0x7E
                        ? static_cast<char>(character)
                        : '?';
                    ++length;
                }
                if (text[length] != L'\0')
                {
                    destination[0] = '\0';
                    return false;
                }
            }
            else
            {
                const char* const text = reinterpret_cast<const char*>(
                    entry + 0x10);
                while (length + 1 < destinationCapacity &&
                    text[length] != '\0')
                {
                    destination[length] = text[length];
                    ++length;
                }
                if (text[length] != '\0')
                {
                    destination[0] = '\0';
                    return false;
                }
            }
            destination[length] = '\0';
            valid = AppendNumberSuffix(
                name.number,
                destination,
                destinationCapacity,
                length);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            destination[0] = '\0';
        }
        return valid;
    }
}
