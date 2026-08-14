#include "HeroAttachableAppearanceComponent.h"

#include "Game/Entity/Native/ThingComponentAccess.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace
{
    using fable::game::hero_pawn::appearance::HeroAppearanceModifierState;

    constexpr std::size_t kCollectionsOffset = 0x3C;
    constexpr std::size_t kCollectionStride = 0x10;
    constexpr std::size_t kCollectionCount = 3;
    constexpr std::size_t kEntryStride = 0x08;
    constexpr std::uintptr_t kExpectedVtableRva = 0x02AF7E34;
    constexpr std::uintptr_t kRemoveModifierRva = 0x019B2F50;
    constexpr std::uintptr_t kAddModifierRva = 0x019B3490;
    constexpr std::uintptr_t kRefreshIfDirtyRva = 0x019B4A10;

    constexpr std::array<std::uint8_t, 4> kRemoveSignature = {
        0x6A, 0xFF, 0x68, 0xB0};
    constexpr std::array<std::uint8_t, 4> kAddSignature = {
        0x6A, 0xFF, 0x68, 0x80};
    constexpr std::array<std::uint8_t, 8> kRefreshSignature = {
        0x56, 0x8B, 0xF1, 0x8A, 0x46, 0x18, 0xA8, 0x01};

    struct ModifierEntry final
    {
        std::int32_t definitionIndex = 0;
        std::int32_t source = 0;
    };

    struct ModifierCollection final
    {
        const ModifierEntry* begin = nullptr;
        const ModifierEntry* end = nullptr;
        const ModifierEntry* capacity = nullptr;
        std::uint32_t metadata = 0;
    };

    static_assert(sizeof(ModifierEntry) == kEntryStride);
    static_assert(sizeof(ModifierCollection) == kCollectionStride);

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
        if (end < cursor)
        {
            return false;
        }
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
            fable::game::entity::native::ThingComponentType::
                HeroAttachableAppearanceModifiers);
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

    bool ReadCollections(
        void* component,
        const ModifierCollection*& collections) noexcept
    {
        collections = nullptr;
        if (!ValidateComponent(component))
        {
            return false;
        }
        __try
        {
            collections = *reinterpret_cast<const ModifierCollection* const*>(
                static_cast<const std::uint8_t*>(component) +
                    kCollectionsOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return IsReadableRange(
            collections, sizeof(ModifierCollection) * kCollectionCount);
    }

    bool CaptureComponent(
        void* component,
        HeroAppearanceModifierState& state) noexcept
    {
        state = {};
        const ModifierCollection* collections = nullptr;
        if (!ReadCollections(component, collections))
        {
            return false;
        }
        __try
        {
            for (std::size_t type = 0; type < kCollectionCount; ++type)
            {
                const ModifierCollection& collection = collections[type];
                if (collection.begin == nullptr || collection.end == nullptr ||
                    collection.end < collection.begin)
                {
                    return false;
                }
                const auto byteCount = reinterpret_cast<std::uintptr_t>(
                    collection.end) - reinterpret_cast<std::uintptr_t>(
                    collection.begin);
                if (byteCount % sizeof(ModifierEntry) != 0 ||
                    (byteCount != 0 &&
                        !IsReadableRange(collection.begin, byteCount)))
                {
                    return false;
                }
                const std::size_t count = byteCount / sizeof(ModifierEntry);
                if (state.count + count > state.MaximumEntries)
                {
                    return false;
                }
                for (std::size_t index = 0; index < count; ++index)
                {
                    const std::int32_t definitionIndex =
                        collection.begin[index].definitionIndex;
                    if (definitionIndex <= 0 || definitionIndex >= 1'000'000 ||
                        state.Contains(definitionIndex))
                    {
                        return false;
                    }
                    state.definitionIndices[state.count++] = definitionIndex;
                }
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
    bool HeroAttachableAppearanceComponent::Capture(
        void* nativeThing,
        HeroAppearanceModifierState& state) noexcept
    {
        return CaptureComponent(FindComponent(nativeThing), state);
    }

    bool HeroAttachableAppearanceComponent::Apply(
        void* nativeThing,
        const HeroAppearanceModifierState& state,
        std::uint32_t* removedCount,
        std::uint32_t* addedCount) noexcept
    {
        if (removedCount != nullptr)
        {
            *removedCount = 0;
        }
        if (addedCount != nullptr)
        {
            *addedCount = 0;
        }
        if (!state.IsSane())
        {
            return false;
        }

        void* const component = FindComponent(nativeThing);
        HeroAppearanceModifierState current;
        if (!CaptureComponent(component, current))
        {
            return false;
        }
        auto* const module = reinterpret_cast<std::uint8_t*>(
            GetModuleHandleW(nullptr));
        auto* const removeAddress = module == nullptr
            ? nullptr
            : module + kRemoveModifierRva;
        auto* const addAddress = module == nullptr
            ? nullptr
            : module + kAddModifierRva;
        auto* const refreshAddress = module == nullptr
            ? nullptr
            : module + kRefreshIfDirtyRva;
        if (!HasSignature(removeAddress, kRemoveSignature) ||
            !HasSignature(addAddress, kAddSignature) ||
            !HasSignature(refreshAddress, kRefreshSignature))
        {
            return false;
        }

        using ModifierFunction = void(__thiscall*)(void*, std::int32_t);
        using RefreshFunction = void(__thiscall*)(void*);
        std::uint32_t removed = 0;
        std::uint32_t added = 0;
        __try
        {
            for (std::size_t index = 0; index < current.count; ++index)
            {
                const std::int32_t definitionIndex =
                    current.definitionIndices[index];
                if (!state.Contains(definitionIndex))
                {
                    reinterpret_cast<ModifierFunction>(removeAddress)(
                        component, definitionIndex);
                    ++removed;
                }
            }
            for (std::size_t index = 0; index < state.count; ++index)
            {
                const std::int32_t definitionIndex =
                    state.definitionIndices[index];
                if (!current.Contains(definitionIndex))
                {
                    reinterpret_cast<ModifierFunction>(addAddress)(
                        component, definitionIndex);
                    ++added;
                }
            }
            if (removed != 0 || added != 0)
            {
                reinterpret_cast<RefreshFunction>(refreshAddress)(component);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        HeroAppearanceModifierState applied;
        if (!CaptureComponent(component, applied) || !applied.Equals(state))
        {
            return false;
        }
        if (removedCount != nullptr)
        {
            *removedCount = removed;
        }
        if (addedCount != nullptr)
        {
            *addedCount = added;
        }
        return true;
    }
}
