#include "CompanionFunctions.h"

#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/Native/Addresses.h"
#include "Game/Native/ScriptTypes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace
{
    using namespace fable::game;
    using namespace fable::game::creature::companion::native;

    constexpr std::uintptr_t kSetFactionRva = 0x01987530;
    constexpr std::uintptr_t kAddAllyRva = 0x01987A00;
    constexpr std::uintptr_t kAddAllySehRva = 0x02503BA0;
    constexpr std::uintptr_t kAddFollowerRva = 0x01AFE0A0;
    constexpr std::uintptr_t kAddFollowerSehRva = 0x0254C381;
    constexpr std::uintptr_t kIntelligentPointerDestructorRva = 0x012E6F60;
    constexpr std::size_t kOwnerThingOffset = 0x04;
    constexpr std::size_t kEnemyFactionOffset = 0x28;
    constexpr std::size_t kEnemyAllyListOffset = 0x1C;
    constexpr std::size_t kEnemyAllyCountOffset = 0x20;
    constexpr std::size_t kRegionFollowerListOffset = 0x0C;
    constexpr std::size_t kRegionFollowerCountOffset = 0x10;
    constexpr std::uint32_t kMaximumRelationshipCount = 512;

    using CharStringConstructor = void(__thiscall*)(
        fable::game::native::CharString*, const char*, int);
    using CharStringDestructor = void(__thiscall*)(
        fable::game::native::CharString*);
    using SetFaction = void(__thiscall*)(
        void* enemy, const fable::game::native::CharString* faction);
    using AddRelationship = void(__thiscall*)(void* component, void* thing);
    using IntelligentPointerDestructor = void(__thiscall*)(void* pointer);
    using GameHeapFree = void(__cdecl*)(void* allocation);

    struct FunctionSet final
    {
        CharStringConstructor constructString = nullptr;
        CharStringDestructor destroyString = nullptr;
        SetFaction setFaction = nullptr;
        AddRelationship addAlly = nullptr;
        AddRelationship addFollower = nullptr;
        IntelligentPointerDestructor destroyIntelligentPointer = nullptr;
        GameHeapFree freeAllocation = nullptr;
    };

    struct NativeListNode final
    {
        NativeListNode* next = nullptr;
        NativeListNode* previous = nullptr;
        void* intelligentPointerVtable = nullptr;
        void* intelligentPointerControl = nullptr;
    };

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
            const std::uintptr_t regionEnd =
                reinterpret_cast<std::uintptr_t>(information.BaseAddress) +
                static_cast<std::uintptr_t>(information.RegionSize);
            if (regionEnd <= cursor)
            {
                return false;
            }
            cursor = (std::min)(regionEnd, end);
        }
        return true;
    }

    bool Matches(
        const void* address,
        const std::uint8_t* bytes,
        std::size_t size) noexcept
    {
        if (!IsReadableRange(address, size))
        {
            return false;
        }
        bool matches = false;
        __try
        {
            matches = std::memcmp(address, bytes, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            matches = false;
        }
        return matches;
    }

    bool ResolveFunctions(HMODULE gameModule, FunctionSet& functions) noexcept
    {
        functions = {};
        if (gameModule == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const setFaction = reinterpret_cast<std::uint8_t*>(
            base + kSetFactionRva);
        auto* const addAlly = reinterpret_cast<std::uint8_t*>(
            base + kAddAllyRva);
        auto* const addFollower = reinterpret_cast<std::uint8_t*>(
            base + kAddFollowerRva);
        auto* const destroyIntelligentPointer =
            reinterpret_cast<std::uint8_t*>(
                base + kIntelligentPointerDestructorRva);
        auto* const constructString = reinterpret_cast<std::uint8_t*>(
            base + fable::game::native::rva::CharStringConstructor);
        auto* const destroyString = reinterpret_cast<std::uint8_t*>(
            base + fable::game::native::rva::CharStringDestructor);
        auto* const freeAllocation = reinterpret_cast<std::uint8_t*>(
            base + fable::game::native::rva::GameHeapFree);

        constexpr std::array<std::uint8_t, 3> setFactionPrefix = {
            0x56, 0x8B, 0xF1,
        };
        constexpr std::array<std::uint8_t, 3> calleeSehPrefix = {
            0x6A, 0xFF, 0x68,
        };
        constexpr std::array<std::uint8_t, 6> pointerDestructorPrefix = {
            0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04,
        };
        constexpr std::array<std::uint8_t, 4> stringConstructorPrefix = {
            0x56, 0x8B, 0xF1, 0xC7,
        };
        constexpr std::array<std::uint8_t, 4> stringDestructorPrefix = {
            0x56, 0x57, 0x8B, 0xF9,
        };
        constexpr std::array<std::uint8_t, 1> heapFreePrefix = {0xE9};

        bool valid = false;
        __try
        {
            valid = Matches(
                        setFaction,
                        setFactionPrefix.data(),
                        setFactionPrefix.size()) &&
                setFaction[27] == 0xC2 &&
                setFaction[28] == 0x04 &&
                setFaction[29] == 0x00 &&
                Matches(
                    addAlly,
                    calleeSehPrefix.data(),
                    calleeSehPrefix.size()) &&
                *reinterpret_cast<const std::uintptr_t*>(addAlly + 3) ==
                    base + kAddAllySehRva &&
                Matches(
                    addFollower,
                    calleeSehPrefix.data(),
                    calleeSehPrefix.size()) &&
                *reinterpret_cast<const std::uintptr_t*>(addFollower + 3) ==
                    base + kAddFollowerSehRva &&
                Matches(
                    destroyIntelligentPointer,
                    pointerDestructorPrefix.data(),
                    pointerDestructorPrefix.size()) &&
                Matches(
                    constructString,
                    stringConstructorPrefix.data(),
                    stringConstructorPrefix.size()) &&
                Matches(
                    destroyString,
                    stringDestructorPrefix.data(),
                    stringDestructorPrefix.size()) &&
                Matches(
                    freeAllocation,
                    heapFreePrefix.data(),
                    heapFreePrefix.size());
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            return false;
        }

        functions.constructString =
            reinterpret_cast<CharStringConstructor>(constructString);
        functions.destroyString =
            reinterpret_cast<CharStringDestructor>(destroyString);
        functions.setFaction = reinterpret_cast<SetFaction>(setFaction);
        functions.addAlly = reinterpret_cast<AddRelationship>(addAlly);
        functions.addFollower = reinterpret_cast<AddRelationship>(addFollower);
        functions.destroyIntelligentPointer =
            reinterpret_cast<IntelligentPointerDestructor>(
                destroyIntelligentPointer);
        functions.freeAllocation =
            reinterpret_cast<GameHeapFree>(freeAllocation);
        return true;
    }

    bool IsOwnedComponent(void* component, void* owner) noexcept
    {
        if (!IsReadableRange(
                component, kOwnerThingOffset + sizeof(void*)))
        {
            return false;
        }
        bool owned = false;
        __try
        {
            owned = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(component) + kOwnerThingOffset) ==
                owner;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            owned = false;
        }
        return owned;
    }

    bool ReadList(
        void* component,
        std::size_t listOffset,
        std::size_t countOffset,
        NativeListNode*& head,
        std::uint32_t& count) noexcept
    {
        head = nullptr;
        count = 0;
        const std::size_t required =
            (std::max)(listOffset + sizeof(void*),
                       countOffset + sizeof(std::uint32_t));
        if (!IsReadableRange(component, required))
        {
            return false;
        }
        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(component);
            head = *reinterpret_cast<NativeListNode**>(bytes + listOffset);
            count = *reinterpret_cast<std::uint32_t*>(bytes + countOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            head = nullptr;
            count = 0;
            return false;
        }
        return head != nullptr && count <= kMaximumRelationshipCount &&
            IsReadableRange(head, sizeof(NativeListNode));
    }

    void* ResolveNodeThing(const NativeListNode* node) noexcept
    {
        if (!IsReadableRange(node, sizeof(NativeListNode)))
        {
            return nullptr;
        }
        void* result = nullptr;
        __try
        {
            void* const control = node->intelligentPointerControl;
            if (control != nullptr &&
                IsReadableRange(control, sizeof(void*) * 2))
            {
                result = *static_cast<void**>(control);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            result = nullptr;
        }
        return result;
    }

    bool FindThingInList(
        void* component,
        std::size_t listOffset,
        std::size_t countOffset,
        void* thing,
        NativeListNode*& result,
        std::uint32_t& count) noexcept
    {
        result = nullptr;
        NativeListNode* head = nullptr;
        if (thing == nullptr ||
            !ReadList(component, listOffset, countOffset, head, count))
        {
            return false;
        }
        NativeListNode* node = nullptr;
        __try
        {
            node = head->next;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        for (std::uint32_t visited = 0;
             node != head && visited <= count;
             ++visited)
        {
            if (!IsReadableRange(node, sizeof(NativeListNode)))
            {
                return false;
            }
            if (ResolveNodeThing(node) == thing)
            {
                result = node;
                return true;
            }
            __try
            {
                node = node->next;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }
        return node == head;
    }

    bool RemoveThingFromList(
        const FunctionSet& functions,
        void* component,
        std::size_t listOffset,
        std::size_t countOffset,
        void* thing) noexcept
    {
        NativeListNode* node = nullptr;
        std::uint32_t count = 0;
        if (!FindThingInList(
                component,
                listOffset,
                countOffset,
                thing,
                node,
                count))
        {
            return false;
        }
        if (node == nullptr)
        {
            return true;
        }
        bool removed = false;
        __try
        {
            NativeListNode* const next = node->next;
            NativeListNode* const previous = node->previous;
            if (next != nullptr && previous != nullptr && count != 0 &&
                next->previous == node && previous->next == node)
            {
                previous->next = next;
                next->previous = previous;
                auto* const countAddress =
                    reinterpret_cast<std::uint32_t*>(
                        static_cast<std::uint8_t*>(component) + countOffset);
                --*countAddress;
                functions.destroyIntelligentPointer(
                    &node->intelligentPointerVtable);
                functions.freeAllocation(node);
                removed = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            removed = false;
        }
        return removed;
    }
}

namespace fable::game::creature::companion::native
{
    bool CompanionFunctions::RegisterWithHero(
        HMODULE gameModule,
        void* followerThing,
        void* heroThing,
        CompanionRegistration& registration) noexcept
    {
        registration = {};
        if (followerThing == nullptr || heroThing == nullptr ||
            followerThing == heroThing)
        {
            return false;
        }

        FunctionSet functions;
        if (!ResolveFunctions(gameModule, functions))
        {
            return false;
        }
        registration.followerEnemy =
            entity::native::ThingComponentAccess::Find(
                followerThing,
                entity::native::ThingComponentType::Enemy);
        registration.heroRegionFollower =
            entity::native::ThingComponentAccess::Find(
                heroThing,
                entity::native::ThingComponentType::RegionFollower);
        if (!IsOwnedComponent(registration.followerEnemy, followerThing) ||
            !IsOwnedComponent(registration.heroRegionFollower, heroThing))
        {
            registration = {};
            return false;
        }

        fable::game::native::CharString faction;
        bool factionConstructed = false;
        bool factionAssigned = false;
        __try
        {
            functions.constructString(&faction, "FACTION_HERO", -1);
            factionConstructed = true;
            functions.setFaction(registration.followerEnemy, &faction);
            factionAssigned = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(registration.followerEnemy) +
                kEnemyFactionOffset) != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            factionAssigned = false;
        }
        if (factionConstructed)
        {
            __try
            {
                functions.destroyString(&faction);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                factionAssigned = false;
            }
        }
        registration.factionAssigned = factionAssigned;
        if (!factionAssigned)
        {
            return false;
        }

        NativeListNode* allyNode = nullptr;
        std::uint32_t allyCount = 0;
        const bool allyListValid = FindThingInList(
            registration.followerEnemy,
            kEnemyAllyListOffset,
            kEnemyAllyCountOffset,
            heroThing,
            allyNode,
            allyCount);
        if (!allyListValid)
        {
            return false;
        }
        if (allyNode == nullptr)
        {
            __try
            {
                functions.addAlly(registration.followerEnemy, heroThing);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
            if (!FindThingInList(
                    registration.followerEnemy,
                    kEnemyAllyListOffset,
                    kEnemyAllyCountOffset,
                    heroThing,
                    allyNode,
                    allyCount) ||
                allyNode == nullptr)
            {
                return false;
            }
        }
        registration.heroAddedAsAlly = true;

        NativeListNode* followerNode = nullptr;
        if (!FindThingInList(
                registration.heroRegionFollower,
                kRegionFollowerListOffset,
                kRegionFollowerCountOffset,
                followerThing,
                followerNode,
                registration.followerCountBefore))
        {
            return false;
        }
        registration.alreadyRegistered = followerNode != nullptr;
        if (followerNode == nullptr)
        {
            __try
            {
                functions.addFollower(
                    registration.heroRegionFollower, followerThing);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }
        if (!FindThingInList(
                registration.heroRegionFollower,
                kRegionFollowerListOffset,
                kRegionFollowerCountOffset,
                followerThing,
                followerNode,
                registration.followerCountAfter) ||
            followerNode == nullptr)
        {
            return false;
        }
        registration.followerRegistered = true;
        return true;
    }

    bool CompanionFunctions::UnregisterFromHero(
        HMODULE gameModule,
        void* followerThing,
        void* heroThing) noexcept
    {
        if (followerThing == nullptr || heroThing == nullptr)
        {
            return true;
        }
        FunctionSet functions;
        if (!ResolveFunctions(gameModule, functions))
        {
            return false;
        }
        void* const regionFollower =
            entity::native::ThingComponentAccess::Find(
                heroThing,
                entity::native::ThingComponentType::RegionFollower);
        return IsOwnedComponent(regionFollower, heroThing) &&
            RemoveThingFromList(
                functions,
                regionFollower,
                kRegionFollowerListOffset,
                kRegionFollowerCountOffset,
                followerThing);
    }
}
