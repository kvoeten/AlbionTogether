#include "HeroClothingComponent.h"

#include "Game/Entity/Native/ThingComponentAccess.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace
{
    using fable::game::hero_pawn::appearance::HeroClothingState;

    constexpr std::size_t kCategoryBeginOffset = 0x24;
    constexpr std::size_t kCategoryEndOffset = 0x28;
    constexpr std::size_t kCategoryStride = 0x30;
    constexpr std::size_t kEntryBeginOffset = 0x08;
    constexpr std::size_t kEntryEndOffset = 0x0C;
    constexpr std::size_t kCategoryDefinitionIndexOffset = 0x20;
    constexpr std::size_t kSelectedIndexOffset = 0x24;
    constexpr std::size_t kMirroredSelectedIndexOffset = 0x28;
    constexpr std::size_t kEntryStride = 0x14;
    constexpr std::size_t kEntryDefinitionIndexOffset = 0x00;
    constexpr std::size_t kEntryCountOffset = 0x04;
    constexpr std::int32_t kFirstCategoryDefinitionIndex = 0xA64;
    constexpr std::size_t kMaximumEntriesPerCategory = 128;

    constexpr std::uintptr_t kExpectedVtableRva = 0x02AFD7EC;
    constexpr std::uintptr_t kWearOwnedDefinitionRva = 0x019F11D7;
    constexpr std::uintptr_t kRebuildAppearanceRva = 0x019F6A45;

    constexpr std::array<std::uint8_t, 3> kWearSignature = {
        0x6A, 0x08, 0xB8};
    constexpr std::array<std::uint8_t, 9> kRebuildSignature = {
        0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x8B, 0x75, 0x08};

    bool IsReadableRange(const void* address, std::size_t bytes) noexcept
    {
        if (address == nullptr || bytes == 0)
        {
            return false;
        }
        auto cursor = reinterpret_cast<std::uintptr_t>(address);
        if (cursor > (std::numeric_limits<std::uintptr_t>::max)() - bytes)
        {
            return false;
        }
        const auto end = cursor + bytes;
        while (cursor < end)
        {
            MEMORY_BASIC_INFORMATION region = {};
            if (VirtualQuery(
                    reinterpret_cast<const void*>(cursor),
                    &region,
                    sizeof(region)) != sizeof(region) ||
                region.State != MEM_COMMIT ||
                (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            {
                return false;
            }
            const auto regionEnd = reinterpret_cast<std::uintptr_t>(
                region.BaseAddress) + region.RegionSize;
            if (regionEnd <= cursor)
            {
                return false;
            }
            cursor = regionEnd < end ? regionEnd : end;
        }
        return true;
    }

    bool IsWritableRange(void* address, std::size_t bytes) noexcept
    {
        if (!IsReadableRange(address, bytes))
        {
            return false;
        }
        MEMORY_BASIC_INFORMATION region = {};
        if (VirtualQuery(address, &region, sizeof(region)) != sizeof(region))
        {
            return false;
        }
        constexpr DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return (region.Protect & writable) != 0;
    }

    template <std::size_t Size>
    bool HasSignature(
        const std::uint8_t* address,
        const std::array<std::uint8_t, Size>& signature) noexcept
    {
        return IsReadableRange(address, Size) &&
            std::memcmp(address, signature.data(), Size) == 0;
    }

    void* FindComponent(void* nativeThing) noexcept
    {
        return fable::game::entity::native::ThingComponentAccess::Find(
            nativeThing,
            fable::game::entity::native::ThingComponentType::InventoryClothing);
    }

    bool ValidateComponent(void* component) noexcept
    {
        auto* const module = reinterpret_cast<std::uint8_t*>(
            GetModuleHandleW(nullptr));
        if (module == nullptr || !IsReadableRange(component, sizeof(void*)))
        {
            return false;
        }
        __try
        {
            return *static_cast<void**>(component) ==
                module + kExpectedVtableRva;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct CategoryView final
    {
        std::uint8_t* record = nullptr;
        std::uint8_t* entries = nullptr;
        std::size_t entryCount = 0;
    };

    bool ReadCategories(
        void* component,
        std::array<CategoryView, HeroClothingState::SlotCount>& views) noexcept
    {
        views = {};
        if (!ValidateComponent(component))
        {
            return false;
        }
        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(component);
            auto* const begin = *reinterpret_cast<std::uint8_t**>(
                bytes + kCategoryBeginOffset);
            auto* const end = *reinterpret_cast<std::uint8_t**>(
                bytes + kCategoryEndOffset);
            if (begin == nullptr || end == nullptr || end < begin ||
                static_cast<std::size_t>(end - begin) !=
                    kCategoryStride * HeroClothingState::SlotCount ||
                !IsReadableRange(
                    begin,
                    kCategoryStride * HeroClothingState::SlotCount))
            {
                return false;
            }
            for (std::size_t slot = 0;
                 slot < HeroClothingState::SlotCount;
                 ++slot)
            {
                std::uint8_t* const category = begin + slot * kCategoryStride;
                const std::int32_t categoryDefinitionIndex =
                    *reinterpret_cast<const std::int32_t*>(
                        category + kCategoryDefinitionIndexOffset);
                if (categoryDefinitionIndex !=
                    kFirstCategoryDefinitionIndex +
                        static_cast<std::int32_t>(slot))
                {
                    return false;
                }
                auto* const entryBegin = *reinterpret_cast<std::uint8_t**>(
                    category + kEntryBeginOffset);
                auto* const entryEnd = *reinterpret_cast<std::uint8_t**>(
                    category + kEntryEndOffset);
                if (entryBegin == nullptr || entryEnd == nullptr ||
                    entryEnd < entryBegin)
                {
                    return false;
                }
                const std::size_t byteCount = static_cast<std::size_t>(
                    entryEnd - entryBegin);
                if (byteCount % kEntryStride != 0 ||
                    byteCount / kEntryStride > kMaximumEntriesPerCategory ||
                    !IsReadableRange(entryBegin, byteCount))
                {
                    return false;
                }
                views[slot] = {
                    category, entryBegin, byteCount / kEntryStride};
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            views = {};
            return false;
        }
        return true;
    }

    bool CaptureComponent(void* component, HeroClothingState& state) noexcept
    {
        state = {};
        std::array<CategoryView, HeroClothingState::SlotCount> categories;
        if (!ReadCategories(component, categories))
        {
            return false;
        }
        __try
        {
            for (std::size_t slot = 0;
                 slot < HeroClothingState::SlotCount;
                 ++slot)
            {
                const CategoryView& category = categories[slot];
                const std::int32_t selectedIndex =
                    *reinterpret_cast<const std::int32_t*>(
                        category.record + kSelectedIndexOffset);
                if (selectedIndex == -1)
                {
                    state.definitionIndices[slot] = -1;
                    continue;
                }
                if (selectedIndex < 0 ||
                    static_cast<std::size_t>(selectedIndex) >=
                        category.entryCount)
                {
                    return false;
                }
                const auto* const entry = category.entries +
                    static_cast<std::size_t>(selectedIndex) * kEntryStride;
                const std::int32_t definitionIndex =
                    *reinterpret_cast<const std::int32_t*>(
                        entry + kEntryDefinitionIndexOffset);
                const std::int32_t count =
                    *reinterpret_cast<const std::int32_t*>(
                        entry + kEntryCountOffset);
                if (definitionIndex <= 0 || definitionIndex >= 1'000'000 ||
                    count <= 0)
                {
                    return false;
                }
                state.definitionIndices[slot] = definitionIndex;
            }
            state.valid = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            state = {};
            return false;
        }
        return state.IsSane();
    }
}

namespace fable::game::hero_pawn::appearance::native
{
    bool HeroClothingComponent::Capture(
        void* nativeThing,
        HeroClothingState& state) noexcept
    {
        return CaptureComponent(FindComponent(nativeThing), state);
    }

    bool HeroClothingComponent::Apply(
        void* nativeThing,
        const HeroClothingState& state,
        std::uint32_t* insertedCount) noexcept
    {
        if (insertedCount != nullptr)
        {
            *insertedCount = 0;
        }
        if (!state.IsSane())
        {
            return false;
        }

        void* const component = FindComponent(nativeThing);
        std::array<CategoryView, HeroClothingState::SlotCount> categories;
        if (!ReadCategories(component, categories))
        {
            return false;
        }
        auto* const module = reinterpret_cast<std::uint8_t*>(
            GetModuleHandleW(nullptr));
        auto* const wearAddress = module == nullptr
            ? nullptr
            : module + kWearOwnedDefinitionRva;
        auto* const rebuildAddress = module == nullptr
            ? nullptr
            : module + kRebuildAppearanceRva;
        if (!HasSignature(wearAddress, kWearSignature) ||
            !HasSignature(rebuildAddress, kRebuildSignature))
        {
            return false;
        }

        using WearFunction = void(__thiscall*)(void*, std::int32_t);
        using RebuildFunction = void(__thiscall*)(void*, void*);
        std::uint32_t inserted = 0;
        __try
        {
            for (std::size_t slot = 0;
                 slot < HeroClothingState::SlotCount;
                 ++slot)
            {
                CategoryView& category = categories[slot];
                const std::int32_t definitionIndex =
                    state.definitionIndices[slot];
                if (definitionIndex == -1)
                {
                    // An empty slot is represented by -1 in the primary field;
                    // Fable mirrors it as zero in the secondary field.
                    *reinterpret_cast<std::int32_t*>(
                        category.record + kSelectedIndexOffset) = -1;
                    *reinterpret_cast<std::int32_t*>(
                        category.record + kMirroredSelectedIndexOffset) = 0;
                    continue;
                }

                std::uint8_t* found = nullptr;
                std::uint8_t* empty = nullptr;
                for (std::size_t index = 0;
                     index < category.entryCount;
                     ++index)
                {
                    auto* const entry =
                        category.entries + index * kEntryStride;
                    const std::int32_t existingDefinition =
                        *reinterpret_cast<const std::int32_t*>(
                            entry + kEntryDefinitionIndexOffset);
                    const std::int32_t existingCount =
                        *reinterpret_cast<const std::int32_t*>(
                            entry + kEntryCountOffset);
                    if (existingDefinition == definitionIndex &&
                        existingCount > 0)
                    {
                        found = entry;
                        break;
                    }
                    if (empty == nullptr && existingDefinition == 0 &&
                        existingCount == 0)
                    {
                        empty = entry;
                    }
                }
                if (found == nullptr)
                {
                    if (empty == nullptr ||
                        !IsWritableRange(empty, kEntryStride))
                    {
                        return false;
                    }
                    std::memset(empty, 0, kEntryStride);
                    *reinterpret_cast<std::int32_t*>(
                        empty + kEntryDefinitionIndexOffset) = definitionIndex;
                    *reinterpret_cast<std::int32_t*>(
                        empty + kEntryCountOffset) = 1;
                    ++inserted;
                }
                reinterpret_cast<WearFunction>(wearAddress)(
                    component, definitionIndex);
            }
            reinterpret_cast<RebuildFunction>(rebuildAddress)(
                component, nativeThing);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        HeroClothingState applied;
        if (!CaptureComponent(component, applied) || !applied.Equals(state))
        {
            return false;
        }
        if (insertedCount != nullptr)
        {
            *insertedCount = inserted;
        }
        return true;
    }
}
