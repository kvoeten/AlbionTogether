#include "CreatureFrameFunctions.h"

#include <cstring>

namespace
{
    bool BytesMatch(
        const void* address,
        const std::uint8_t* expected,
        std::size_t size) noexcept
    {
        if (address == nullptr || expected == nullptr || size == 0)
        {
            return false;
        }
        __try
        {
            return std::memcmp(address, expected, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool HasVtable(void* nativeThing, const void* expected) noexcept
    {
        if (nativeThing == nullptr || expected == nullptr)
        {
            return false;
        }
        __try
        {
            return *static_cast<void**>(nativeThing) == expected;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace fable::game::creature::native
{
    bool CreatureFrameFunctions::ValidateImplementations(
        HMODULE gameModule) noexcept
    {
        if (gameModule == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        const auto* const creatureUpdate = reinterpret_cast<const std::uint8_t*>(
            base + CreatureUpdateFrameRva);
        const auto* const playerUpdate = reinterpret_cast<const std::uint8_t*>(
            base + PlayerCreatureUpdateFrameRva);
        return BytesMatch(
                   creatureUpdate,
                   CreatureUpdateFrameExpectedPrefix.data(),
                   CreatureUpdateFrameExpectedPrefix.size()) &&
            BytesMatch(
                creatureUpdate + CreatureUpdateFrameSuffixOffset,
                CreatureUpdateFrameExpectedSuffix.data(),
                CreatureUpdateFrameExpectedSuffix.size()) &&
            BytesMatch(
                playerUpdate,
                PlayerUpdateFrameExpectedPrefix.data(),
                PlayerUpdateFrameExpectedPrefix.size());
    }

    bool CreatureFrameFunctions::ResolveCreatureUpdateFrameSlot(
        HMODULE gameModule,
        void*** slot,
        UpdateFramePointer& function) noexcept
    {
        if (slot != nullptr)
        {
            *slot = nullptr;
        }
        function = nullptr;
        if (gameModule == nullptr || slot == nullptr ||
            !ValidateImplementations(gameModule))
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto** const vtable = reinterpret_cast<void**>(base + CreatureVtableRva);
        auto** const candidateSlot = vtable + UpdateFrameVtableSlot;
        const auto candidate = reinterpret_cast<UpdateFramePointer>(
            base + CreatureUpdateFrameRva);
        bool valid = false;
        __try
        {
            valid = *candidateSlot == reinterpret_cast<void*>(candidate);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            return false;
        }
        *slot = candidateSlot;
        function = candidate;
        return true;
    }

    bool CreatureFrameFunctions::ResolvePlayerUpdateFrameSlot(
        HMODULE gameModule,
        void*** slot,
        UpdateFramePointer& function) noexcept
    {
        if (slot != nullptr)
        {
            *slot = nullptr;
        }
        function = nullptr;
        if (gameModule == nullptr || slot == nullptr ||
            !ValidateImplementations(gameModule))
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto** const vtable = reinterpret_cast<void**>(
            base + PlayerCreatureVtableRva);
        auto** const candidateSlot = vtable + UpdateFrameVtableSlot;
        const auto candidate = reinterpret_cast<UpdateFramePointer>(
            base + PlayerCreatureUpdateFrameRva);
        bool valid = false;
        __try
        {
            valid = *candidateSlot == reinterpret_cast<void*>(candidate);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            return false;
        }
        *slot = candidateSlot;
        function = candidate;
        return true;
    }

    bool CreatureFrameFunctions::ValidateCreature(
        HMODULE gameModule,
        void* nativeThing) noexcept
    {
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        return gameModule != nullptr && HasVtable(
            nativeThing,
            reinterpret_cast<const void*>(base + CreatureVtableRva));
    }

    bool CreatureFrameFunctions::ValidatePlayerCreature(
        HMODULE gameModule,
        void* nativeThing) noexcept
    {
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        return gameModule != nullptr && HasVtable(
            nativeThing,
            reinterpret_cast<const void*>(base + PlayerCreatureVtableRva));
    }
}
