#include "ThingComponentAccess.h"

#include <Windows.h>

#include <cstdint>
#include <limits>

namespace
{
    constexpr std::size_t kMaximumComponentCount = 512;

    bool IsReadableRange(const void* address, std::size_t bytes) noexcept
    {
        if (address == nullptr || bytes == 0)
        {
            return false;
        }

        const auto start = reinterpret_cast<std::uintptr_t>(address);
        if (start > (std::numeric_limits<std::uintptr_t>::max)() - bytes)
        {
            return false;
        }

        const auto end = start + bytes;
        auto cursor = start;
        while (cursor < end)
        {
            MEMORY_BASIC_INFORMATION information = {};
            if (VirtualQuery(
                    reinterpret_cast<const void*>(cursor),
                    &information,
                    sizeof(information)) != sizeof(information) ||
                information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
            {
                return false;
            }

            const auto regionStart = reinterpret_cast<std::uintptr_t>(
                information.BaseAddress);
            const auto regionEnd = regionStart + information.RegionSize;
            if (regionEnd <= cursor)
            {
                return false;
            }
            cursor = regionEnd < end ? regionEnd : end;
        }
        return true;
    }
}

namespace fable::game::entity::native
{
    bool ThingComponentAccess::ReadRange(
        void* nativeThing,
        ThingComponentRange& range) noexcept
    {
        range = {};
        if (!IsReadableRange(
                nativeThing,
                ComponentRangeOffset + (sizeof(void*) * 2)))
        {
            return false;
        }

        const auto* thingBytes = static_cast<const std::uint8_t*>(nativeThing);
        const ThingComponentEntry* begin = nullptr;
        const ThingComponentEntry* end = nullptr;
        __try
        {
            begin = *reinterpret_cast<const ThingComponentEntry* const*>(
                thingBytes + ComponentRangeOffset);
            end = *reinterpret_cast<const ThingComponentEntry* const*>(
                thingBytes + ComponentRangeOffset + sizeof(void*));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        if (begin == nullptr || end == nullptr || end < begin)
        {
            return false;
        }

        const auto byteCount = reinterpret_cast<std::uintptr_t>(end) -
            reinterpret_cast<std::uintptr_t>(begin);
        if (byteCount % sizeof(ThingComponentEntry) != 0)
        {
            return false;
        }

        const std::size_t count = byteCount / sizeof(ThingComponentEntry);
        if (count == 0 || count > kMaximumComponentCount ||
            !IsReadableRange(begin, byteCount))
        {
            return false;
        }

        range.begin = begin;
        range.end = end;
        range.count = count;
        return true;
    }

    void* ThingComponentAccess::Find(
        void* nativeThing,
        ThingComponentType type) noexcept
    {
        ThingComponentRange range;
        if (!ReadRange(nativeThing, range))
        {
            return nullptr;
        }

        const auto requestedType = static_cast<std::int32_t>(type);
        std::size_t first = 0;
        std::size_t remaining = range.count;
        __try
        {
            while (remaining != 0)
            {
                const std::size_t half = remaining / 2;
                const std::size_t middle = first + half;
                if (range.begin[middle].type < requestedType)
                {
                    first = middle + 1;
                    remaining -= half + 1;
                }
                else
                {
                    remaining = half;
                }
            }

            if (first < range.count &&
                range.begin[first].type == requestedType)
            {
                return range.begin[first].instance;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
        return nullptr;
    }

    bool ThingComponentAccess::Has(
        void* nativeThing,
        ThingComponentType type) noexcept
    {
        return Find(nativeThing, type) != nullptr;
    }
}
