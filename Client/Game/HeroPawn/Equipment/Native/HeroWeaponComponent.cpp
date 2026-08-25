#include "HeroWeaponComponent.h"

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
    constexpr std::uintptr_t kEquipWeaponRva = 0x01A4AA23;
    constexpr std::uintptr_t kReconcilePresentationRva = 0x01A492BD;
    constexpr std::uintptr_t kRequestDestroyRva = 0x01B2E530;

    constexpr std::array<std::uint8_t, 6> kResolvePointerSignature = {
        0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04};
    constexpr std::array<std::uint8_t, 7> kAssignPointerSignature = {
        0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04, 0x57};
    // The immediate following this prologue is an ASLR-relocated exception
    // metadata address, so it cannot be part of a runtime byte signature.
    constexpr std::array<std::uint8_t, 3> kEquipWeaponSignature = {
        0x6A, 0x14, 0xB8};
    constexpr std::array<std::uint8_t, 7> kReconcileSignature = {
        0x68, 0x00, 0x01, 0x00, 0x00, 0xB8, 0x2A};
    constexpr std::array<std::uint8_t, 6> kRequestDestroySignature = {
        0x56, 0x8B, 0xF1, 0x8A, 0x86, 0x9D};

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
    using EquipWeapon = void(__thiscall*)(void*, std::int32_t, int);
    using ReconcilePresentation = void(__thiscall*)(void*);
    using RequestDestroy = void(__thiscall*)(void*, bool);

    struct Functions final
    {
        ResolvePointer resolve = nullptr;
        AssignPointer assign = nullptr;
        EquipWeapon equip = nullptr;
        ReconcilePresentation reconcile = nullptr;
        RequestDestroy requestDestroy = nullptr;
    };

    bool ResolveFunctions(
        HMODULE gameModule,
        Functions& functions,
        std::uint32_t* signatureMask = nullptr) noexcept
    {
        functions = {};
        auto* const module = reinterpret_cast<std::uint8_t*>(gameModule);
        if (signatureMask != nullptr)
        {
            *signatureMask = 0;
        }
        if (module == nullptr)
        {
            return false;
        }
        const bool resolveReady = HasSignature(
            module + kResolvePointerRva, kResolvePointerSignature);
        const bool assignReady = HasSignature(
            module + kAssignPointerRva, kAssignPointerSignature);
        const bool equipReady = HasSignatureOrDetour(
            module + kEquipWeaponRva, kEquipWeaponSignature);
        const bool reconcileReady = HasSignatureOrDetour(
            module + kReconcilePresentationRva, kReconcileSignature);
        const bool destroyReady = HasSignatureOrDetour(
            module + kRequestDestroyRva, kRequestDestroySignature);
        const std::uint32_t mask =
            (resolveReady ? 1u : 0u) |
            (assignReady ? 2u : 0u) |
            (equipReady ? 4u : 0u) |
            (reconcileReady ? 8u : 0u) |
            (destroyReady ? 16u : 0u);
        if (signatureMask != nullptr)
        {
            *signatureMask = mask;
        }
        if (mask != 0x1Fu)
        {
            return false;
        }
        functions.resolve = reinterpret_cast<ResolvePointer>(
            module + kResolvePointerRva);
        functions.assign = reinterpret_cast<AssignPointer>(
            module + kAssignPointerRva);
        functions.equip = reinterpret_cast<EquipWeapon>(
            module + kEquipWeaponRva);
        functions.reconcile = reinterpret_cast<ReconcilePresentation>(
            module + kReconcilePresentationRva);
        functions.requestDestroy = reinterpret_cast<RequestDestroy>(
            module + kRequestDestroyRva);
        return true;
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
        void* component,
        std::size_t pointerOffset,
        std::size_t definitionOffset,
        std::int32_t requestedDefinition,
        const Functions& functions) noexcept
    {
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
        if (requestedDefinition != -1)
        {
            functions.equip(component, requestedDefinition, 0);
            current = functions.resolve(slot);
            return ReadDefinition(current, currentDefinition) &&
                currentDefinition == requestedDefinition;
        }

        functions.assign(slot, nullptr);
        *reinterpret_cast<std::int32_t*>(bytes + definitionOffset) = -1;
        functions.reconcile(component);
        if (current != nullptr)
        {
            functions.requestDestroy(current, true);
        }
        return functions.resolve(slot) == nullptr;
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
            gameModule, functions, &inspection.functionSignatureMask);
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
        void* nativeThing,
        const HeroEquipmentState& state) noexcept
    {
        if (!ApplyDefinitions(nativeThing, state))
        {
            return false;
        }
        HeroEquipmentState verified;
        return Capture(nativeThing, verified) && verified.Equals(state);
    }

    bool HeroWeaponComponent::ApplyDefinitions(
        void* nativeThing,
        const HeroEquipmentState& state) noexcept
    {
        if (!state.IsSane())
        {
            return false;
        }
        HMODULE const gameModule = GetModuleHandleW(nullptr);
        Functions functions;
        void* const component = FindComponent(nativeThing, gameModule);
        if (component == nullptr || !ResolveFunctions(gameModule, functions))
        {
            return false;
        }
        bool applied = false;
        __try
        {
            applied = ApplySlot(
                    component,
                    kMeleePointerOffset,
                    kMeleeDefinitionOffset,
                    state.meleeDefinitionIndex,
                    functions) &&
                ApplySlot(
                    component,
                    kRangedPointerOffset,
                    kRangedDefinitionOffset,
                    state.rangedDefinitionIndex,
                    functions);
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

}
