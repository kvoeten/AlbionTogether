#include "HeroWeaponComponent.h"

#include "Game/Creature/Equipment/Native/CreatureWeaponNativeSupport.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/Native/Addresses.h"
#include "Game/Native/GameInterface.h"
#include "Game/Native/ScriptTypes.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr std::size_t kMeleePointerOffset = 0x188;
    constexpr std::size_t kRangedPointerOffset = 0x195;
    constexpr std::size_t kMeleeDefinitionOffset = 0x1B8;
    constexpr std::size_t kRangedDefinitionOffset = 0x1BC;
    constexpr std::size_t kThingDefinitionIndexOffset = 0x98;
    constexpr std::size_t kCarryingEntriesBeginOffset = 0x0C;
    constexpr std::size_t kCarryingEntriesEndOffset = 0x10;
    constexpr std::size_t kCarryingEntrySize = 12;
    constexpr std::size_t kCarryingEntryThingOffset = 4;
    constexpr std::uint32_t kRightHandSlot = 923;
    constexpr std::uint32_t kLeftHandSlot = 924;
    constexpr std::uint32_t kBothHandsSlot = 925;

    constexpr std::uintptr_t kResolvePointerRva = 0x012E6EA0;
    constexpr std::uintptr_t kAssignPointerRva = 0x012E6EE0;
    constexpr std::uintptr_t kInventoryItemCountRva = 0x019F1108;
    constexpr std::uintptr_t kAddItemToInventoryRva = 0x019F2375;
    constexpr std::uintptr_t kOnItemAddedRva = 0x01A4B43C;
    constexpr std::size_t kAddItemToInventoryVtableOffset = 0x14C;
    constexpr std::size_t kOnItemAddedVtableOffset = 0x158;

    constexpr std::array<std::uint8_t, 6> kResolvePointerSignature = {
        0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04};
    constexpr std::array<std::uint8_t, 7> kAssignPointerSignature = {
        0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04, 0x57};
    // The immediate after B8 is an ASLR-relocated exception metadata address.
    constexpr std::array<std::uint8_t, 3> kInventoryItemCountSignature = {
        0x6A, 0x08, 0xB8};
    constexpr std::array<std::uint8_t, 7> kAddItemToInventorySignature = {
        0x55, 0x8B, 0xEC, 0x53, 0x8B, 0x5D, 0x08};
    constexpr std::array<std::uint8_t, 3> kOnItemAddedSignature = {
        0x6A, 0x44, 0xB8};

    template <std::size_t Size>
    bool HasSignature(
        const std::uint8_t* address,
        const std::array<std::uint8_t, Size>& signature) noexcept
    {
        return address != nullptr &&
            std::memcmp(address, signature.data(), Size) == 0;
    }

    template <std::size_t Size>
    bool HasSignatureOrDetour(
        const std::uint8_t* address,
        const std::array<std::uint8_t, Size>& signature) noexcept
    {
        return HasSignature(address, signature) ||
            (address != nullptr && address[0] == 0xE9);
    }

    void* FindComponent(void* nativeThing, HMODULE gameModule) noexcept
    {
        return fable::game::entity::native::ThingComponentAccess::
            FindByVtableRva(
                nativeThing,
                gameModule,
                fable::game::hero_pawn::equipment::native::
                    HeroWeaponComponent::ExpectedVtableRva);
    }

    using ResolvePointer = void* (__thiscall*)(void*);
    using AssignPointer = void(__thiscall*)(void*, void*);
    using InventoryItemCount = std::int32_t(__thiscall*)(
        void*, std::int32_t);
    using AddItemToInventory = bool(__thiscall*)(
        void*, std::int32_t, std::int32_t, bool, bool, std::int32_t, bool);
    using OnItemAdded = void(__thiscall*)(
        void*, std::int32_t, bool, bool);

    struct Functions final
    {
        ResolvePointer resolve = nullptr;
        AssignPointer assign = nullptr;
        InventoryItemCount itemCount = nullptr;
        AddItemToInventory addItem = nullptr;
        OnItemAdded onItemAdded = nullptr;
    };

    bool ResolveFunctions(
        HMODULE gameModule,
        void* component,
        Functions& functions,
        std::uint32_t* signatureMask = nullptr) noexcept
    {
        functions = {};
        auto* const module = reinterpret_cast<std::uint8_t*>(gameModule);
        if (signatureMask != nullptr)
        {
            *signatureMask = 0;
        }
        if (module == nullptr || component == nullptr)
        {
            return false;
        }
        const bool resolveReady = HasSignature(
            module + kResolvePointerRva, kResolvePointerSignature);
        const bool assignReady = HasSignature(
            module + kAssignPointerRva, kAssignPointerSignature);
        const bool itemCountReady = HasSignatureOrDetour(
            module + kInventoryItemCountRva,
            kInventoryItemCountSignature);
        const bool addItemReady = HasSignatureOrDetour(
            module + kAddItemToInventoryRva,
            kAddItemToInventorySignature);
        const bool onItemAddedReady = HasSignatureOrDetour(
            module + kOnItemAddedRva,
            kOnItemAddedSignature);
        bool vtableReady = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(component);
            vtableReady = vtable != nullptr &&
                vtable[kAddItemToInventoryVtableOffset / sizeof(void*)] ==
                    module + kAddItemToInventoryRva &&
                vtable[kOnItemAddedVtableOffset / sizeof(void*)] ==
                    module + kOnItemAddedRva;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            vtableReady = false;
        }
        const std::uint32_t mask =
            (resolveReady ? 1u : 0u) |
            (assignReady ? 2u : 0u) |
            (itemCountReady ? 4u : 0u) |
            (addItemReady ? 8u : 0u) |
            (onItemAddedReady ? 16u : 0u) |
            (vtableReady ? 32u : 0u);
        if (signatureMask != nullptr)
        {
            *signatureMask = mask;
        }
        if (mask != 0x3Fu)
        {
            return false;
        }
        functions.resolve = reinterpret_cast<ResolvePointer>(
            module + kResolvePointerRva);
        functions.assign = reinterpret_cast<AssignPointer>(
            module + kAssignPointerRva);
        functions.itemCount = reinterpret_cast<InventoryItemCount>(
            module + kInventoryItemCountRva);
        functions.addItem = reinterpret_cast<AddItemToInventory>(
            module + kAddItemToInventoryRva);
        functions.onItemAdded = reinterpret_cast<OnItemAdded>(
            module + kOnItemAddedRva);
        return true;
    }

    bool QuarantineDetachedWeapon(
        fable::game::EntityService& entities,
        void* weapon) noexcept
    {
        using fable::game::creature::equipment::native::detail::ReadThingUid;
        if (weapon == nullptr)
        {
            return true;
        }
        const std::uint64_t uid = ReadThingUid(weapon);
        fable::game::Entity* const entity = uid != 0
            ? entities.FindByUid(uid)
            : nullptr;
        if (entity == nullptr)
        {
            return false;
        }
        const bool collidable = entity->SetCollidable(false);
        const bool drawable = entity->SetDrawable(false);
        entity->Release();
        return collidable && drawable;
    }

    bool ReadDefinition(void* weapon, std::int32_t& definitionIndex) noexcept
    {
        definitionIndex = -1;
        if (weapon == nullptr)
        {
            return true;
        }
        __try
        {
            definitionIndex = static_cast<std::int32_t>(
                *reinterpret_cast<const std::uint16_t*>(
                    static_cast<const std::uint8_t*>(weapon) +
                        kThingDefinitionIndexOffset));
            // InventoryWeapons can retain an allocated empty-slot thing whose
            // definition index is zero. On the wire it is the same semantic
            // state as an absent smart pointer.
            if (definitionIndex == 0)
            {
                definitionIndex = -1;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return definitionIndex == -1 ||
            (definitionIndex > 0 && definitionIndex < 1'000'000);
    }

    bool ReadPresentationState(
        void* carrying,
        const Functions& functions,
        std::int32_t meleeDefinitionIndex,
        std::int32_t rangedDefinitionIndex,
        std::uint32_t& meleeAttachmentSlot,
        std::uint32_t& rangedAttachmentSlot,
        fable::game::creature::equipment::CreatureWeaponFamily& family)
        noexcept
    {
        using fable::game::creature::equipment::CreatureWeaponFamily;
        family = CreatureWeaponFamily::None;
        meleeAttachmentSlot = 0;
        rangedAttachmentSlot = 0;
        if (carrying == nullptr || functions.resolve == nullptr)
        {
            return false;
        }
        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(carrying);
            auto* const begin = *reinterpret_cast<std::uint8_t**>(
                bytes + kCarryingEntriesBeginOffset);
            auto* const end = *reinterpret_cast<std::uint8_t**>(
                bytes + kCarryingEntriesEndOffset);
            if (begin == nullptr || end == nullptr || end < begin ||
                static_cast<std::size_t>(end - begin) % kCarryingEntrySize != 0 ||
                static_cast<std::size_t>(end - begin) /
                        kCarryingEntrySize > 128)
            {
                return begin == end;
            }
            for (auto* entry = begin; entry != end;
                 entry += kCarryingEntrySize)
            {
                const std::uint32_t slot =
                    *reinterpret_cast<const std::uint32_t*>(entry);
                void* const weapon = functions.resolve(
                    entry + kCarryingEntryThingOffset);
                std::int32_t definitionIndex = -1;
                if (!ReadDefinition(weapon, definitionIndex))
                {
                    continue;
                }
                if (rangedDefinitionIndex > 0 &&
                    definitionIndex == rangedDefinitionIndex)
                {
                    rangedAttachmentSlot = slot;
                    if (slot == kRightHandSlot || slot == kLeftHandSlot ||
                        slot == kBothHandsSlot)
                    {
                        family = CreatureWeaponFamily::Ranged;
                    }
                }
                if (meleeDefinitionIndex > 0 &&
                    definitionIndex == meleeDefinitionIndex)
                {
                    meleeAttachmentSlot = slot;
                    if (family == CreatureWeaponFamily::None &&
                        (slot == kRightHandSlot || slot == kLeftHandSlot ||
                            slot == kBothHandsSlot))
                    {
                        family = CreatureWeaponFamily::Melee;
                    }
                }
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            family = CreatureWeaponFamily::None;
            meleeAttachmentSlot = 0;
            rangedAttachmentSlot = 0;
            return false;
        }
    }

    bool ApplySlot(
        fable::game::EntityService& entities,
        void* hero,
        void* component,
        std::size_t pointerOffset,
        std::size_t definitionOffset,
        std::int32_t requestedDefinition,
        const Functions& functions,
        const fable::game::creature::equipment::native::detail::Functions&
            creatureFunctions,
        bool& mutated) noexcept
    {
        mutated = false;
        auto* const bytes = static_cast<std::uint8_t*>(component);
        void* const slot = bytes + pointerOffset;
        void* current = functions.resolve(slot);
        std::int32_t currentDefinition = -1;
        if (!ReadDefinition(current, currentDefinition))
        {
            return false;
        }
        if (currentDefinition == requestedDefinition)
        {
            return true;
        }

        if (currentDefinition == -1 && requestedDefinition != -1)
        {
            // The public inventory mutation owns the complete Hero path. It
            // changes the count and synchronously calls the derived
            // CTCHeroInventoryWeapons::OnItemAdded handler, which constructs
            // the inventory Thing and its presentation graph. The lower-level
            // equip helper assumes the count already exists and is not a
            // valid insertion boundary.
            const std::int32_t count = functions.itemCount(
                component, requestedDefinition);
            if (count <= 0)
            {
                if (!functions.addItem(
                        component,
                        requestedDefinition,
                        1,
                        false,
                        true,
                        0,
                        false))
                {
                    return false;
                }
            }
            else
            {
                // Recover an interrupted restore without incrementing the
                // inventory count twice. OnItemAdded rebuilds the missing
                // family slot from the already-owned definition.
                functions.onItemAdded(
                    component, requestedDefinition, true, false);
            }
            current = functions.resolve(slot);
            if (!ReadDefinition(current, currentDefinition) ||
                currentDefinition != requestedDefinition)
            {
                return false;
            }
            // Build one inventory weapon per update. This keeps native side
            // effects ordered and lets the caller verify the completed slot
            // before advancing to the other family.
            mutated = true;
            return false;
        }

        // CTCCarrying can still own and asynchronously inspect this Thing
        // after CTCHeroInventoryWeapons releases its pointer. Detach the
        // visible carrying entry first, then ask Fable to retire the Thing at
        // its normal safe lifecycle boundary. Immediate destruction clears
        // the native vtable while the population worker can still have the
        // old pointer queued, which turns a later virtual call into an AV.
        // Quarantine the map-owned replacement rather than explicitly
        // destroying it; the map lifecycle remains its final owner.
        using namespace fable::game::creature::equipment::native::detail;
        if (!QuarantineDetachedWeapon(entities, current))
        {
            return false;
        }
        void* const carrying = FindCarrying(hero);
        std::uint32_t attachmentSlot = 0;
        if (carrying != nullptr &&
            ReadAttachmentSlot(
                creatureFunctions, carrying, current, attachmentSlot))
        {
            creatureFunctions.remove(carrying, current);
        }
        functions.assign(slot, nullptr);
        *reinterpret_cast<std::int32_t*>(bytes + definitionOffset) = -1;
        if (functions.resolve(slot) != nullptr)
        {
            return false;
        }
        mutated = true;
        return requestedDefinition == -1;
    }
}

namespace fable::game::hero_pawn::equipment::native
{
    bool HeroWeaponComponent::RequestActiveFamily(
        game::native::GameInterfaceAccess& interfaceAccess,
        const game::native::ScriptThing& hero,
        game::creature::equipment::CreatureWeaponFamily family) noexcept
    {
        using game::creature::equipment::CreatureWeaponFamily;
        if (family != CreatureWeaponFamily::None &&
            family != CreatureWeaponFamily::Melee &&
            family != CreatureWeaponFamily::Ranged)
        {
            return false;
        }

        std::size_t slot = game::native::game_interface_slot::SheatheWeapons;
        std::uintptr_t expectedRva = game::native::rva::SheatheWeapons;
        if (family == CreatureWeaponFamily::Melee)
        {
            slot = game::native::game_interface_slot::UnsheatheMeleeWeapon;
            expectedRva = game::native::rva::UnsheatheMeleeWeapon;
        }
        else if (family == CreatureWeaponFamily::Ranged)
        {
            slot = game::native::game_interface_slot::UnsheatheRangedWeapon;
            expectedRva = game::native::rva::UnsheatheRangedWeapon;
        }

        auto* const gameInterface = interfaceAccess.Resolve();
        void* const address = interfaceAccess.ResolveFunction(
            slot, expectedRva);
        if (gameInterface == nullptr || address == nullptr)
        {
            return false;
        }

        using RequestWeaponPresentation = void(__thiscall*)(
            game::native::GameScriptInterface*,
            const game::native::ScriptThing*,
            bool);
        bool requested = false;
        __try
        {
            reinterpret_cast<RequestWeaponPresentation>(address)(
                gameInterface, &hero, false);
            requested = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            requested = false;
        }
        return requested;
    }

    bool HeroWeaponComponent::Capture(
        void* nativeThing,
        HeroEquipmentState& state) noexcept
    {
        state = {};
        HeroWeaponInspection inspection;
        if (!Inspect(nativeThing, inspection) ||
            !inspection.functionsResolved || !inspection.readable)
        {
            return false;
        }
        state.meleeDefinitionIndex = inspection.meleeDefinitionIndex;
        state.rangedDefinitionIndex = inspection.rangedDefinitionIndex;
        state.meleeAttachmentSlot = inspection.meleeAttachmentSlot;
        state.rangedAttachmentSlot = inspection.rangedAttachmentSlot;
        state.activeFamily = inspection.activeFamily;
        state.valid = true;
        return state.IsSane();
    }

    bool HeroWeaponComponent::Inspect(
        void* nativeThing,
        HeroWeaponInspection& inspection) noexcept
    {
        inspection = {};
        inspection.meleeDefinitionIndex = -2;
        inspection.rangedDefinitionIndex = -2;
        HMODULE const gameModule = GetModuleHandleW(nullptr);
        Functions functions;
        inspection.component = FindComponent(nativeThing, gameModule);
        inspection.carryingComponent =
            fable::game::entity::native::ThingComponentAccess::Find(
                nativeThing,
                fable::game::entity::native::ThingComponentType::Carrying);
        inspection.functionsResolved = ResolveFunctions(
            gameModule,
            inspection.component,
            functions,
            &inspection.functionSignatureMask);
        if (inspection.component == nullptr || !inspection.functionsResolved)
        {
            return true;
        }
        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(
                inspection.component);
            inspection.meleeWeapon = functions.resolve(
                bytes + kMeleePointerOffset);
            inspection.rangedWeapon = functions.resolve(
                bytes + kRangedPointerOffset);
            inspection.readable = ReadDefinition(
                    inspection.meleeWeapon,
                    inspection.meleeDefinitionIndex) &&
                ReadDefinition(
                    inspection.rangedWeapon,
                    inspection.rangedDefinitionIndex) &&
                ReadPresentationState(
                    inspection.carryingComponent,
                    functions,
                    inspection.meleeDefinitionIndex,
                    inspection.rangedDefinitionIndex,
                    inspection.meleeAttachmentSlot,
                    inspection.rangedAttachmentSlot,
                    inspection.activeFamily);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            inspection.readable = false;
        }
        return true;
    }

    bool HeroWeaponComponent::Apply(
        game::EntityService& entities,
        void* nativeThing,
        const HeroEquipmentState& state) noexcept
    {
        if (!ApplyDefinitions(entities, nativeThing, state))
        {
            return false;
        }
        HeroEquipmentState verified;
        return Capture(nativeThing, verified) && verified.Equals(state);
    }

    bool HeroWeaponComponent::ApplyDefinitions(
        game::EntityService& entities,
        void* nativeThing,
        const HeroEquipmentState& state) noexcept
    {
        if (!state.IsSane())
        {
            return false;
        }
        HMODULE const gameModule = GetModuleHandleW(nullptr);
        Functions functions;
        game::creature::equipment::native::detail::Functions
            creatureFunctions;
        void* const component = FindComponent(nativeThing, gameModule);
        if (component == nullptr ||
            !ResolveFunctions(gameModule, component, functions) ||
            !game::creature::equipment::native::detail::ResolveFunctions(
                gameModule, creatureFunctions))
        {
            return false;
        }
        bool applied = false;
        __try
        {
            bool mutated = false;
            const bool meleeApplied = ApplySlot(
                    entities,
                    nativeThing,
                    component,
                    kMeleePointerOffset,
                    kMeleeDefinitionOffset,
                    state.meleeDefinitionIndex,
                    functions,
                    creatureFunctions,
                    mutated);
            if (mutated)
            {
                return false;
            }
            applied = meleeApplied && ApplySlot(
                    entities,
                    nativeThing,
                    component,
                    kRangedPointerOffset,
                    kRangedDefinitionOffset,
                    state.rangedDefinitionIndex,
                    functions,
                    creatureFunctions,
                    mutated);
            if (mutated)
            {
                return false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }
        if (!applied)
        {
            return false;
        }
        HeroWeaponInspection verified;
        // This operation owns only the inventory definitions. A freshly
        // provisioned remote Hero can have both smart pointers initialized
        // before CTCCarrying exposes a complete readable presentation; the
        // caller applies and verifies those carry slots separately.
        return Inspect(nativeThing, verified) &&
            verified.meleeDefinitionIndex == state.meleeDefinitionIndex &&
            verified.rangedDefinitionIndex == state.rangedDefinitionIndex;
    }

    bool HeroWeaponComponent::ApplyPresentation(
        game::EntityService& entities,
        void* nativeThing,
        const HeroEquipmentState& state) noexcept
    {
        using namespace game::creature::equipment::native::detail;
        if (nativeThing == nullptr || !state.IsSane())
        {
            return false;
        }

        HeroWeaponInspection inventory;
        game::creature::equipment::native::detail::Functions functions;
        HMODULE const gameModule = GetModuleHandleW(nullptr);
        if (!Inspect(nativeThing, inventory) || !inventory.readable ||
            inventory.meleeDefinitionIndex != state.meleeDefinitionIndex ||
            inventory.rangedDefinitionIndex != state.rangedDefinitionIndex ||
            !game::creature::equipment::native::detail::ResolveFunctions(
                gameModule, functions))
        {
            return false;
        }

        const auto setVisibility = [&entities](
            void* weapon,
            const bool visible) noexcept
        {
            if (weapon == nullptr)
            {
                return true;
            }
            const std::uint64_t uid = ReadThingUid(weapon);
            game::Entity* const entity = uid != 0
                ? entities.FindByUid(uid)
                : nullptr;
            if (entity == nullptr)
            {
                return false;
            }
            const bool collidable = entity->SetCollidable(false);
            const bool drawable = entity->SetDrawable(visible);
            entity->Release();
            return collidable && drawable;
        };
        const auto place = [&](
            void* weapon,
            const std::uint32_t currentSlot,
            const std::uint32_t requestedSlot) noexcept
        {
            if (weapon == nullptr)
            {
                return requestedSlot == 0;
            }
            if (requestedSlot != 0 &&
                !AttachmentSlotAvailable(functions, requestedSlot))
            {
                return false;
            }
            if (currentSlot != requestedSlot)
            {
                if (currentSlot != 0)
                {
                    functions.remove(inventory.carryingComponent, weapon);
                }
                if (requestedSlot != 0)
                {
                    functions.attach(
                        inventory.carryingComponent,
                        weapon,
                        requestedSlot,
                        true);
                }
            }
            return setVisibility(weapon, requestedSlot != 0);
        };

        bool applied = false;
        __try
        {
            applied = place(
                    inventory.meleeWeapon,
                    inventory.meleeAttachmentSlot,
                    state.meleeAttachmentSlot) &&
                place(
                    inventory.rangedWeapon,
                    inventory.rangedAttachmentSlot,
                    state.rangedAttachmentSlot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }
        if (!applied)
        {
            return false;
        }

        HeroWeaponInspection verified;
        return Inspect(nativeThing, verified) && verified.readable &&
            verified.meleeDefinitionIndex == state.meleeDefinitionIndex &&
            verified.rangedDefinitionIndex == state.rangedDefinitionIndex &&
            verified.meleeAttachmentSlot == state.meleeAttachmentSlot &&
            verified.rangedAttachmentSlot == state.rangedAttachmentSlot &&
            verified.activeFamily == state.activeFamily;
    }

}
