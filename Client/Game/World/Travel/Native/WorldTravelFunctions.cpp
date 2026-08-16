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
