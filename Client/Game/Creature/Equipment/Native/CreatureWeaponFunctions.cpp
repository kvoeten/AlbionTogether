#include "CreatureWeaponFunctions.h"

#include "Core/Hooking/CodePatch.h"
#include "Game/Entity/Native/ThingComponentAccess.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr std::uintptr_t kContainsWeaponRva = 0x01954250;
    constexpr std::uintptr_t kFindWeaponRva = 0x019542D0;
    constexpr std::uintptr_t kSheatheWeaponsRva = 0x01B408B0;
    constexpr std::uintptr_t kEquipWeaponRva = 0x01B409E0;
    constexpr std::uintptr_t kResolvePointerRva = 0x012E6EA0;
    constexpr std::uintptr_t kGetDefinitionRva = 0x016F0930;
    constexpr std::uintptr_t kResolveWeaponPropertiesRva = 0x016F2CB0;
    constexpr std::uintptr_t kAttachWeaponRva = 0x01956650;
    constexpr std::uintptr_t kRemoveWeaponRva = 0x01955AA0;
    constexpr std::uintptr_t kRequestDestroyRva = 0x01B2E530;
    constexpr std::uintptr_t kAttachmentSlotRegistryRva = 0x03228090;
    constexpr std::uintptr_t kResolveAttachmentSlotRva = 0x01719230;
    constexpr std::uintptr_t kAttachmentReferenceGuardRva = 0x019568F0;
    constexpr std::uintptr_t kAttachmentReferencePresentResumeRva =
        0x019568F7;
    constexpr std::uintptr_t kAttachmentReferenceMissingResumeRva =
        0x0195690A;
    constexpr std::uintptr_t kAttachmentGraphicGuardRva = 0x01956AA7;
    constexpr std::uintptr_t kAttachmentGraphicPresentResumeRva =
        0x01956AAF;
    constexpr std::uintptr_t kAttachmentGraphicMissingResumeRva =
        0x01956B1D;
    constexpr std::uintptr_t kSheatheExceptionHandlerRva = 0x02553938;
    constexpr std::uintptr_t kEquipExceptionHandlerRva = 0x02553970;
    constexpr std::uintptr_t kResolvePropertiesExceptionHandlerRva =
        0x0252A6D0;
    constexpr std::uintptr_t kAttachWeaponExceptionHandlerRva = 0x0252A968;
    constexpr std::uintptr_t kResolveAttachmentSlotExceptionHandlerRva =
        0x0250E558;

    constexpr std::size_t kCarryingEntriesBeginOffset = 0x0C;
    constexpr std::size_t kCarryingEntriesEndOffset = 0x10;
    constexpr std::size_t kCarryingEntrySize = 12;
    constexpr std::size_t kCarryingEntryThingOffset = 4;
    constexpr std::size_t kStowedSlotOffset = 0x30;
    constexpr std::size_t kThingDefinitionIndexOffset = 0x98;

    constexpr std::array<std::uint8_t, 16> kContainsWeaponSignature = {
        0x53, 0x55, 0x56, 0x8B, 0xF1, 0x8B, 0x4E, 0x10,
        0x2B, 0x4E, 0x0C, 0xB8, 0xAB, 0xAA, 0xAA, 0x2A};
    constexpr std::array<std::uint8_t, 16> kFindWeaponSignature = {
        0x53, 0x55, 0x56, 0x8B, 0xF1, 0x8B, 0x4E, 0x10,
        0x2B, 0x4E, 0x0C, 0xB8, 0xAB, 0xAA, 0xAA, 0x2A};
    constexpr std::array<std::uint8_t, 14> kSheatheBodySignature = {
        0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50,
        0x83, 0xEC, 0x18, 0x53, 0x55, 0x56, 0x57};
    constexpr std::array<std::uint8_t, 12> kEquipBodySignature = {
        0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
        0x50, 0x51, 0x53, 0x55, 0x56, 0x57};
    constexpr std::array<std::uint8_t, 6> kResolvePointerSignature = {
        0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04};
    constexpr std::array<std::uint8_t, 15> kGetDefinitionSignature = {
        0x51, 0x8B, 0x49, 0x7C, 0x8B, 0x44, 0x24, 0x08,
        0xC7, 0x04, 0x24, 0x00, 0x00, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 14> kResolvePropertiesBodySignature = {
        0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50,
        0x83, 0xEC, 0x08, 0x53, 0x56, 0x57, 0xA1};
    constexpr std::array<std::uint8_t, 14> kAttachWeaponBodySignature = {
        0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50,
        0x83, 0xEC, 0x28, 0x53, 0x55, 0x56, 0x57};
    constexpr std::array<std::uint8_t, 5> kRemoveWeaponSignature = {
        0x53, 0x8B, 0x5C, 0x24, 0x08};
    constexpr std::array<std::uint8_t, 6> kRequestDestroySignature = {
        0x56, 0x8B, 0xF1, 0x8A, 0x86, 0x9D};
    constexpr std::array<std::uint8_t, 8>
        kResolveAttachmentSlotBodySignature = {
            0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x56};
    constexpr std::array<std::uint8_t, 7>
        kAttachmentReferenceGuardSignature = {
            0x8B, 0x4C, 0x24, 0x4C, 0x8A, 0x59, 0x30};
    constexpr std::array<std::uint8_t, 8>
        kAttachmentGraphicGuardSignature = {
            0x8B, 0x74, 0x24, 0x54, 0x8D, 0x44, 0x24, 0x24};

    std::uintptr_t gAttachmentReferencePresentResume = 0;
    std::uintptr_t gAttachmentReferenceMissingResume = 0;
    std::uintptr_t gAttachmentGraphicPresentResume = 0;
    std::uintptr_t gAttachmentGraphicMissingResume = 0;
    SRWLOCK gAttachmentReferenceGuardLock = SRWLOCK_INIT;
    fable::core::hooking::CodePatch gAttachmentReferenceGuardPatch;
    fable::core::hooking::CodePatch gAttachmentGraphicGuardPatch;

#if defined(_M_IX86)
    __declspec(naked) void AttachmentReferenceGuard()
    {
        __asm
        {
            mov ecx, dword ptr [esp + 4Ch]
            test ecx, ecx
            jz missing_reference
            mov bl, byte ptr [ecx + 30h]
            jmp dword ptr [gAttachmentReferencePresentResume]
        missing_reference:
            xor ebx, ebx
            jmp dword ptr [gAttachmentReferenceMissingResume]
        }
    }

    __declspec(naked) void AttachmentGraphicGuard()
    {
        __asm
        {
            mov esi, dword ptr [esp + 54h]
            test esi, esi
            jz missing_graphic_reference
            lea eax, [esp + 24h]
            jmp dword ptr [gAttachmentGraphicPresentResume]
        missing_graphic_reference:
            jmp dword ptr [gAttachmentGraphicMissingResume]
        }
    }
#endif

    using ContainsWeapon = bool(__thiscall*)(
        void* carryingComponent,
        std::int32_t definitionIndex);
    using FindWeapon = void* (__thiscall*)(
        void* carryingComponent,
        std::int32_t definitionIndex);
    using SheatheWeapons = void(__thiscall*)(void* creature);
    using EquipWeapon = void(__thiscall*)(
        void* creature,
        std::int32_t definitionIndex,
        bool primary);
    using ResolvePointer = void* (__thiscall*)(void* pointer);
    using GetDefinition = void(__thiscall*)(void* weapon, void** definition);
    using ResolveWeaponProperties = bool(__thiscall*)(
        void* definition,
        void** properties);
    using AttachWeapon = void(__thiscall*)(
        void* carryingComponent,
        void* weapon,
        std::uint32_t slot,
        bool refresh);
    using RemoveWeapon = std::uintptr_t(__thiscall*)(
        void* carryingComponent,
        void* weapon);
    using RequestDestroy = void(__thiscall*)(void* thing, bool immediate);
    using ResolveAttachmentSlot = bool(__thiscall*)(
        void* registry,
        std::uint32_t slot,
        void** definition);

    struct Functions final
    {
        ContainsWeapon contains = nullptr;
        FindWeapon find = nullptr;
        SheatheWeapons sheathe = nullptr;
        EquipWeapon equip = nullptr;
        ResolvePointer resolvePointer = nullptr;
        GetDefinition getDefinition = nullptr;
        ResolveWeaponProperties resolveProperties = nullptr;
        AttachWeapon attach = nullptr;
        RemoveWeapon remove = nullptr;
        void** attachmentSlotRegistry = nullptr;
        ResolveAttachmentSlot resolveAttachmentSlot = nullptr;
    };

    template <std::size_t Size>
    bool BytesMatch(
        const std::uint8_t* address,
        const std::array<std::uint8_t, Size>& expected) noexcept
    {
        if (address == nullptr)
        {
            return false;
        }
        __try
        {
            return std::memcmp(address, expected.data(), Size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    template <std::size_t BodySize>
    bool ValidateSehFunction(
        const std::uint8_t* address,
        std::uintptr_t expectedExceptionHandler,
        const std::array<std::uint8_t, BodySize>& bodySignature) noexcept
    {
        if (address == nullptr)
        {
            return false;
        }
        __try
        {
            std::uintptr_t exceptionHandler = 0;
            if (address[0] != 0x6A || address[1] != 0xFF ||
                address[2] != 0x68)
            {
                return false;
            }
            std::memcpy(
                &exceptionHandler,
                address + 3,
                sizeof(exceptionHandler));
            return exceptionHandler == expectedExceptionHandler &&
                BytesMatch(address + 7, bodySignature);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsRelativeDetour(const std::uint8_t* address) noexcept
    {
        if (address == nullptr)
        {
            return false;
        }
        __try
        {
            return address[0] == 0xE9;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool EnsureAttachmentReferenceGuard(HMODULE gameModule) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        return false;
#else
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const target = reinterpret_cast<std::uint8_t*>(
            base + kAttachmentReferenceGuardRva);
        auto* const graphicTarget = reinterpret_cast<std::uint8_t*>(
            base + kAttachmentGraphicGuardRva);
        AcquireSRWLockExclusive(&gAttachmentReferenceGuardLock);
        gAttachmentReferencePresentResume =
            base + kAttachmentReferencePresentResumeRva;
        gAttachmentReferenceMissingResume =
            base + kAttachmentReferenceMissingResumeRva;
        gAttachmentGraphicPresentResume =
            base + kAttachmentGraphicPresentResumeRva;
        gAttachmentGraphicMissingResume =
            base + kAttachmentGraphicMissingResumeRva;
        bool ready = gAttachmentReferenceGuardPatch.IsInstalled();
        bool installedReferenceNow = false;
        if (!ready)
        {
            ready = gAttachmentReferenceGuardPatch.InstallRelativeJump(
                target,
                kAttachmentReferenceGuardSignature.data(),
                kAttachmentReferenceGuardSignature.size(),
                reinterpret_cast<void*>(&AttachmentReferenceGuard),
                kAttachmentReferenceGuardSignature.size());
            installedReferenceNow = ready;
        }
        if (ready && !gAttachmentGraphicGuardPatch.IsInstalled())
        {
            ready = gAttachmentGraphicGuardPatch.InstallRelativeJump(
                graphicTarget,
                kAttachmentGraphicGuardSignature.data(),
                kAttachmentGraphicGuardSignature.size(),
                reinterpret_cast<void*>(&AttachmentGraphicGuard),
                kAttachmentGraphicGuardSignature.size());
            if (!ready && installedReferenceNow)
            {
                (void)gAttachmentReferenceGuardPatch.Shutdown();
            }
        }
        ready = ready && gAttachmentReferenceGuardPatch.IsInstalled() &&
            gAttachmentGraphicGuardPatch.IsInstalled();
        ReleaseSRWLockExclusive(&gAttachmentReferenceGuardLock);
        return ready;
#endif
    }

    bool ResolveFunctions(
        HMODULE gameModule,
        Functions& functions,
        std::uint32_t* signatureMask = nullptr) noexcept
    {
        functions = {};
        if (signatureMask != nullptr)
        {
            *signatureMask = 0;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        if (base == 0)
        {
            return false;
        }

        auto* const contains = reinterpret_cast<std::uint8_t*>(
            base + kContainsWeaponRva);
        auto* const find = reinterpret_cast<std::uint8_t*>(
            base + kFindWeaponRva);
        auto* const sheathe = reinterpret_cast<std::uint8_t*>(
            base + kSheatheWeaponsRva);
        auto* const equip = reinterpret_cast<std::uint8_t*>(
            base + kEquipWeaponRva);
        auto* const resolvePointer = reinterpret_cast<std::uint8_t*>(
            base + kResolvePointerRva);
        auto* const getDefinition = reinterpret_cast<std::uint8_t*>(
            base + kGetDefinitionRva);
        auto* const resolveProperties = reinterpret_cast<std::uint8_t*>(
            base + kResolveWeaponPropertiesRva);
        auto* const attach = reinterpret_cast<std::uint8_t*>(
            base + kAttachWeaponRva);
        auto* const remove = reinterpret_cast<std::uint8_t*>(
            base + kRemoveWeaponRva);
        auto* const resolveAttachmentSlot =
            reinterpret_cast<std::uint8_t*>(
                base + kResolveAttachmentSlotRva);
        const bool containsReady = BytesMatch(
            contains, kContainsWeaponSignature);
        const bool findReady = BytesMatch(find, kFindWeaponSignature);
        const bool sheatheReady = ValidateSehFunction(
            sheathe,
            base + kSheatheExceptionHandlerRva,
            kSheatheBodySignature);
        const bool equipReady = ValidateSehFunction(
            equip,
            base + kEquipExceptionHandlerRva,
            kEquipBodySignature);
        const bool resolvePointerReady = BytesMatch(
            resolvePointer, kResolvePointerSignature);
        const bool getDefinitionReady = BytesMatch(
            getDefinition, kGetDefinitionSignature);
        const bool resolvePropertiesReady = ValidateSehFunction(
            resolveProperties,
            base + kResolvePropertiesExceptionHandlerRva,
            kResolvePropertiesBodySignature);
        const bool attachReady = IsRelativeDetour(attach) ||
            ValidateSehFunction(
                attach,
                base + kAttachWeaponExceptionHandlerRva,
                kAttachWeaponBodySignature);
        const bool removeReady = IsRelativeDetour(remove) ||
            BytesMatch(remove, kRemoveWeaponSignature);
        // RemoteHeroDefinitionHook detours this shared definition lookup only
        // while constructing a remote Hero, then forwards every unrelated
        // lookup through its validated trampoline. Treat that owned hook the
        // same way as the carrying observers above instead of disabling the
        // entire weapon stack after remote-Hero support is installed.
        const bool resolveAttachmentSlotReady =
            IsRelativeDetour(resolveAttachmentSlot) ||
            ValidateSehFunction(
                resolveAttachmentSlot,
                base + kResolveAttachmentSlotExceptionHandlerRva,
                kResolveAttachmentSlotBodySignature);
        const bool attachmentReferenceGuardReady =
            EnsureAttachmentReferenceGuard(gameModule);
        const std::uint32_t mask =
            (containsReady ? 1u : 0u) |
            (findReady ? 2u : 0u) |
            (sheatheReady ? 4u : 0u) |
            (equipReady ? 8u : 0u) |
            (resolvePointerReady ? 16u : 0u) |
            (getDefinitionReady ? 32u : 0u) |
            (resolvePropertiesReady ? 64u : 0u) |
            (attachReady ? 128u : 0u) |
            (removeReady ? 256u : 0u) |
            (resolveAttachmentSlotReady ? 512u : 0u) |
            (attachmentReferenceGuardReady ? 1024u : 0u);
        if (signatureMask != nullptr)
        {
            *signatureMask = mask;
        }
        if (mask != 0x7FFu)
        {
            return false;
        }

        functions.contains = reinterpret_cast<ContainsWeapon>(contains);
        functions.find = reinterpret_cast<FindWeapon>(find);
        functions.sheathe = reinterpret_cast<SheatheWeapons>(sheathe);
        functions.equip = reinterpret_cast<EquipWeapon>(equip);
        functions.resolvePointer = reinterpret_cast<ResolvePointer>(
            resolvePointer);
        functions.getDefinition = reinterpret_cast<GetDefinition>(
            getDefinition);
        functions.resolveProperties =
            reinterpret_cast<ResolveWeaponProperties>(resolveProperties);
        functions.attach = reinterpret_cast<AttachWeapon>(attach);
        functions.remove = reinterpret_cast<RemoveWeapon>(remove);
        functions.attachmentSlotRegistry = reinterpret_cast<void**>(
            base + kAttachmentSlotRegistryRva);
        functions.resolveAttachmentSlot =
            reinterpret_cast<ResolveAttachmentSlot>(resolveAttachmentSlot);
        return true;
    }

    bool IsSaneDefinition(std::int32_t definitionIndex) noexcept
    {
        return definitionIndex == -1 ||
            (definitionIndex > 0 && definitionIndex < 1'000'000);
    }

    void* FindCarrying(void* creature) noexcept
    {
        return fable::game::entity::native::ThingComponentAccess::Find(
            creature,
            fable::game::entity::native::ThingComponentType::Carrying);
    }

    bool Contains(
        const Functions& functions,
        void* carrying,
        std::int32_t definitionIndex) noexcept
    {
        return definitionIndex > 0 &&
            functions.contains(carrying, definitionIndex);
    }

    void ReadWeaponPresentation(
        void* weapon,
        void*& graphic) noexcept
    {
        graphic = nullptr;
        if (weapon == nullptr)
        {
            return;
        }
        __try
        {
            // CThing's render-graph owner. CTCCarrying queues attachment work
            // against this object after it binds the weapon to the creature.
            graphic = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(weapon) + 0xA4);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            graphic = nullptr;
        }
    }

    void ReadThingDefinition(
        void* thing,
        std::int32_t& definitionIndex) noexcept
    {
        definitionIndex = -1;
        if (thing == nullptr)
        {
            return;
        }
        __try
        {
            definitionIndex = static_cast<std::int32_t>(
                *reinterpret_cast<const std::uint16_t*>(
                    static_cast<const std::uint8_t*>(thing) +
                        kThingDefinitionIndexOffset));
            if (!IsSaneDefinition(definitionIndex))
            {
                definitionIndex = -1;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            definitionIndex = -1;
        }
    }

    void ReleaseReference(void* reference) noexcept
    {
        if (reference == nullptr)
        {
            return;
        }
        __try
        {
            auto* const count = reinterpret_cast<std::uint32_t*>(
                static_cast<std::uint8_t*>(reference) + 4);
            if (--(*count) == 0)
            {
                auto* const vtable = *reinterpret_cast<void***>(reference);
                using Destroy = void(__thiscall*)(void*);
                reinterpret_cast<Destroy>(vtable[1])(reference);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool AttachmentSlotAvailable(
        const Functions& functions,
        std::uint32_t slot) noexcept
    {
        if (slot == 0 || functions.attachmentSlotRegistry == nullptr ||
            functions.resolveAttachmentSlot == nullptr)
        {
            return false;
        }
        void* definition = nullptr;
        bool available = false;
        __try
        {
            void* const registry = *functions.attachmentSlotRegistry;
            available = registry != nullptr &&
                functions.resolveAttachmentSlot(
                    registry, slot, &definition) &&
                definition != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            available = false;
        }
        ReleaseReference(definition);
        return available;
    }

    bool ReadStowedSlot(
        const Functions& functions,
        void* weapon,
        std::uint32_t& slot) noexcept
    {
        slot = 0;
        if (weapon == nullptr)
        {
            return false;
        }
        void* definition = nullptr;
        void* properties = nullptr;
        bool resolved = false;
        __try
        {
            functions.getDefinition(weapon, &definition);
            resolved = definition != nullptr &&
                functions.resolveProperties(definition, &properties) &&
                properties != nullptr;
            if (resolved)
            {
                slot = *reinterpret_cast<const std::uint32_t*>(
                    static_cast<const std::uint8_t*>(properties) +
                        kStowedSlotOffset);
                resolved = slot > 0 && slot < 1'000'000;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            resolved = false;
            slot = 0;
        }
        ReleaseReference(properties);
        ReleaseReference(definition);
        return resolved;
    }

    bool ReadAttachmentSlot(
        const Functions& functions,
        void* carrying,
        void* weapon,
        std::uint32_t& slot) noexcept
    {
        slot = 0;
        if (carrying == nullptr || weapon == nullptr)
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
                static_cast<std::size_t>(end - begin) %
                        kCarryingEntrySize != 0 ||
                static_cast<std::size_t>(end - begin) /
                        kCarryingEntrySize > 128)
            {
                return false;
            }
            for (auto* entry = begin; entry != end;
                 entry += kCarryingEntrySize)
            {
                if (functions.resolvePointer(
                        entry + kCarryingEntryThingOffset) == weapon)
                {
                    slot = *reinterpret_cast<const std::uint32_t*>(entry);
                    return true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            slot = 0;
        }
        return false;
    }

}

namespace fable::game::creature::equipment::native
{
    bool CreatureWeaponFunctions::PruneUnexpectedWeapons(
        void* creature,
        std::int32_t allowedMeleeDefinitionIndex,
        std::uint32_t allowedMeleeAttachmentSlot,
        std::int32_t allowedRangedDefinitionIndex,
        std::uint32_t allowedRangedAttachmentSlot,
        std::size_t& removedCount) noexcept
    {
        removedCount = 0;
        if (creature == nullptr ||
            !IsSaneDefinition(allowedMeleeDefinitionIndex) ||
            !IsSaneDefinition(allowedRangedDefinitionIndex))
        {
            return false;
        }
        HMODULE const gameModule = GetModuleHandleW(nullptr);
        Functions functions;
        void* const carrying = FindCarrying(creature);
        auto* const requestDestroyAddress =
            reinterpret_cast<std::uint8_t*>(gameModule) +
            kRequestDestroyRva;
        if (carrying == nullptr ||
            !ResolveFunctions(gameModule, functions) ||
            !(IsRelativeDetour(requestDestroyAddress) ||
                BytesMatch(
                    requestDestroyAddress, kRequestDestroySignature)))
        {
            return false;
        }
        const auto requestDestroy = reinterpret_cast<RequestDestroy>(
            requestDestroyAddress);
        struct Removal final
        {
            void* thing = nullptr;
            std::int32_t definitionIndex = -1;
            std::uint32_t slot = 0;
        };
        std::array<Removal, CreatureCarryingInspection::Capacity> removals = {};
        std::size_t removalCount = 0;
        bool readable = false;
        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(carrying);
            auto* const begin = *reinterpret_cast<std::uint8_t**>(
                bytes + kCarryingEntriesBeginOffset);
            auto* const end = *reinterpret_cast<std::uint8_t**>(
                bytes + kCarryingEntriesEndOffset);
            if (begin == nullptr || end == nullptr || end < begin ||
                static_cast<std::size_t>(end - begin) %
                        kCarryingEntrySize != 0 ||
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
                // 923-929 are Fable's Hero hand/back weapon attachment slots.
                if (slot < 923 || slot > 929)
                {
                    continue;
                }
                void* const thing = functions.resolvePointer(
                    entry + kCarryingEntryThingOffset);
                std::int32_t definitionIndex = -1;
                ReadThingDefinition(thing, definitionIndex);
                const bool allowedMelee =
                    allowedMeleeAttachmentSlot != 0 &&
                    definitionIndex == allowedMeleeDefinitionIndex;
                const bool allowedRanged =
                    allowedRangedAttachmentSlot != 0 &&
                    definitionIndex == allowedRangedDefinitionIndex;
                if (thing == nullptr || definitionIndex <= 0 ||
                    allowedMelee || allowedRanged)
                {
                    continue;
                }
                if (removalCount >= removals.size())
                {
                    return false;
                }
                removals[removalCount++] = {
                    thing, definitionIndex, slot};
            }
            readable = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }
        if (!readable)
        {
            return false;
        }
        __try
        {
            for (std::size_t index = 0; index < removalCount; ++index)
            {
                functions.remove(carrying, removals[index].thing);
                requestDestroy(removals[index].thing, false);
                ++removedCount;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return true;
    }

    bool CreatureWeaponFunctions::InspectCarrying(
        void* creature,
        CreatureCarryingInspection& inspection) noexcept
    {
        inspection = {};
        if (creature == nullptr)
        {
            return false;
        }
        HMODULE const gameModule = GetModuleHandleW(nullptr);
        Functions functions;
        void* const carrying = FindCarrying(creature);
        if (carrying == nullptr || !ResolveFunctions(gameModule, functions))
        {
            return false;
        }
        bool readable = false;
        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(carrying);
            auto* const begin = *reinterpret_cast<std::uint8_t**>(
                bytes + kCarryingEntriesBeginOffset);
            auto* const end = *reinterpret_cast<std::uint8_t**>(
                bytes + kCarryingEntriesEndOffset);
            if (begin == nullptr || end == nullptr || end < begin ||
                static_cast<std::size_t>(end - begin) %
                        kCarryingEntrySize != 0 ||
                static_cast<std::size_t>(end - begin) /
                        kCarryingEntrySize > 128)
            {
                return begin == end;
            }
            for (auto* entry = begin; entry != end;
                 entry += kCarryingEntrySize)
            {
                if (inspection.count >= inspection.entries.size())
                {
                    inspection.truncated = true;
                    break;
                }
                CreatureCarryingEntry& captured =
                    inspection.entries[inspection.count++];
                captured.attachmentSlot =
                    *reinterpret_cast<const std::uint32_t*>(entry);
                captured.thing = functions.resolvePointer(
                    entry + kCarryingEntryThingOffset);
                ReadThingDefinition(
                    captured.thing, captured.definitionIndex);
                ReadWeaponPresentation(captured.thing, captured.graphic);
            }
            readable = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            inspection = {};
            readable = false;
        }
        return readable;
    }

    bool CreatureWeaponFunctions::Inspect(
        void* creature,
        std::int32_t meleeDefinitionIndex,
        std::int32_t rangedDefinitionIndex,
        CreatureWeaponInspection& inspection) noexcept
    {
        inspection = {};
        if (creature == nullptr ||
            !IsSaneDefinition(meleeDefinitionIndex) ||
            !IsSaneDefinition(rangedDefinitionIndex))
        {
            return false;
        }

        HMODULE const gameModule = GetModuleHandleW(nullptr);
        Functions functions;
        inspection.functionsResolved = ResolveFunctions(
            gameModule,
            functions,
            &inspection.functionSignatureMask);
        inspection.carryingComponent = FindCarrying(creature);
        if (!inspection.functionsResolved ||
            inspection.carryingComponent == nullptr)
        {
            return true;
        }

        bool readable = false;
        __try
        {
            inspection.meleePresent = Contains(
                functions,
                inspection.carryingComponent,
                meleeDefinitionIndex);
            inspection.rangedPresent = Contains(
                functions,
                inspection.carryingComponent,
                rangedDefinitionIndex);
            inspection.meleeWeapon = meleeDefinitionIndex > 0
                ? functions.find(
                    inspection.carryingComponent, meleeDefinitionIndex)
                : nullptr;
            inspection.rangedWeapon = rangedDefinitionIndex > 0
                ? functions.find(
                    inspection.carryingComponent, rangedDefinitionIndex)
                : nullptr;
            ReadWeaponPresentation(
                inspection.meleeWeapon, inspection.meleeGraphic);
            ReadWeaponPresentation(
                inspection.rangedWeapon, inspection.rangedGraphic);
            ReadStowedSlot(
                functions, inspection.meleeWeapon,
                inspection.meleeStowedSlot);
            ReadStowedSlot(
                functions, inspection.rangedWeapon,
                inspection.rangedStowedSlot);
            ReadAttachmentSlot(
                functions, inspection.carryingComponent,
                inspection.meleeWeapon, inspection.meleeAttachmentSlot);
            ReadAttachmentSlot(
                functions, inspection.carryingComponent,
                inspection.rangedWeapon, inspection.rangedAttachmentSlot);
            inspection.meleeStowed = inspection.meleePresent &&
                inspection.meleeStowedSlot != 0 &&
                inspection.meleeAttachmentSlot == inspection.meleeStowedSlot;
            inspection.rangedStowed = inspection.rangedPresent &&
                inspection.rangedStowedSlot != 0 &&
                inspection.rangedAttachmentSlot == inspection.rangedStowedSlot;
            readable = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }
        return readable;
    }

    bool CreatureWeaponFunctions::ApplyLoadout(
        void* creature,
        std::int32_t meleeDefinitionIndex,
        std::int32_t rangedDefinitionIndex,
        std::uint32_t meleeAttachmentSlot,
        std::uint32_t rangedAttachmentSlot,
        CreatureWeaponFamily activeFamily,
        CreatureWeaponInspection* inspection) noexcept
    {
        if (inspection != nullptr)
        {
            *inspection = {};
        }
        if (creature == nullptr ||
            !IsSaneDefinition(meleeDefinitionIndex) ||
            !IsSaneDefinition(rangedDefinitionIndex))
        {
            return false;
        }

        if (activeFamily != CreatureWeaponFamily::None &&
            activeFamily != CreatureWeaponFamily::Melee &&
            activeFamily != CreatureWeaponFamily::Ranged)
        {
            return false;
        }

        if ((activeFamily == CreatureWeaponFamily::Melee &&
                meleeDefinitionIndex <= 0) ||
            (activeFamily == CreatureWeaponFamily::Ranged &&
                rangedDefinitionIndex <= 0) ||
            (meleeDefinitionIndex == -1
                ? meleeAttachmentSlot != 0
                : meleeAttachmentSlot >= 1'000'000) ||
            (rangedDefinitionIndex == -1
                ? rangedAttachmentSlot != 0
                : rangedAttachmentSlot >= 1'000'000))
        {
            return false;
        }

        HMODULE const gameModule = GetModuleHandleW(nullptr);
        Functions functions;
        std::uint32_t signatureMask = 0;
        void* const carrying = FindCarrying(creature);
        if (carrying == nullptr ||
            !ResolveFunctions(gameModule, functions, &signatureMask))
        {
            if (inspection != nullptr)
            {
                inspection->carryingComponent = carrying;
                inspection->functionSignatureMask = signatureMask;
                inspection->functionsResolved = false;
            }
            return false;
        }

        bool applied = false;
        __try
        {
            const bool meleePresent = Contains(
                functions, carrying, meleeDefinitionIndex);
            const bool rangedPresent = Contains(
                functions, carrying, rangedDefinitionIndex);
            if (meleeDefinitionIndex > 0 && !meleePresent)
            {
                functions.equip(creature, meleeDefinitionIndex, true);
            }
            if (rangedDefinitionIndex > 0 && !rangedPresent)
            {
                // The definition supplies the weapon family. This flag picks
                // its primary presentation slot; false selects the alternate
                // slot that causes Hero bows to remain world pickups.
                functions.equip(creature, rangedDefinitionIndex, true);
            }
            void* const meleeWeapon = meleeDefinitionIndex > 0
                ? functions.find(carrying, meleeDefinitionIndex)
                : nullptr;
            void* const rangedWeapon = rangedDefinitionIndex > 0
                ? functions.find(carrying, rangedDefinitionIndex)
                : nullptr;
            const bool definitionsReady =
                (meleeDefinitionIndex <= 0 || meleeWeapon != nullptr) &&
                (rangedDefinitionIndex <= 0 || rangedWeapon != nullptr) &&
                (meleeDefinitionIndex <= 0 || Contains(
                    functions, carrying, meleeDefinitionIndex)) &&
                (rangedDefinitionIndex <= 0 || Contains(
                    functions, carrying, rangedDefinitionIndex));
            if (definitionsReady)
            {
                CreatureWeaponInspection current;
                const bool inspected = Inspect(
                    creature,
                    meleeDefinitionIndex,
                    rangedDefinitionIndex,
                    current);
                const std::uint32_t requestedActiveSlot =
                    activeFamily == CreatureWeaponFamily::Melee
                        ? meleeAttachmentSlot
                        : activeFamily == CreatureWeaponFamily::Ranged
                            ? rangedAttachmentSlot
                            : 0;
                std::uint32_t activeSlot =
                    AttachmentSlotAvailable(functions, requestedActiveSlot)
                    ? requestedActiveSlot
                    : 0;
                if (activeSlot == 0 && current.meleePresent &&
                    !current.meleeStowed && AttachmentSlotAvailable(
                        functions, current.meleeAttachmentSlot))
                {
                    activeSlot = current.meleeAttachmentSlot;
                }
                else if (activeSlot == 0 && current.rangedPresent &&
                    !current.rangedStowed && AttachmentSlotAvailable(
                        functions, current.rangedAttachmentSlot))
                {
                    activeSlot = current.rangedAttachmentSlot;
                }
                const auto availableStowedSlot = [&functions](
                    std::uint32_t targetSlot,
                    std::uint32_t ownerFallback)
                {
                    if (AttachmentSlotAvailable(functions, targetSlot))
                    {
                        return targetSlot;
                    }
                    return AttachmentSlotAvailable(
                            functions, ownerFallback)
                        ? ownerFallback
                        : 0u;
                };
                const std::uint32_t targetMeleeSlot =
                    activeFamily == CreatureWeaponFamily::Melee
                        ? activeSlot
                        : availableStowedSlot(
                            current.meleeStowedSlot,
                            meleeAttachmentSlot);
                const std::uint32_t targetRangedSlot =
                    activeFamily == CreatureWeaponFamily::Ranged
                        ? activeSlot
                        : availableStowedSlot(
                            current.rangedStowedSlot,
                            rangedAttachmentSlot);
                const bool familyReady =
                    activeFamily == CreatureWeaponFamily::None
                        ? (meleeDefinitionIndex <= 0 ||
                                current.meleeStowedSlot == 0 ||
                                current.meleeStowed) &&
                            (rangedDefinitionIndex <= 0 ||
                                current.rangedStowedSlot == 0 ||
                                current.rangedStowed)
                        : activeFamily == CreatureWeaponFamily::Melee
                            ? current.meleePresent &&
                                !current.meleeStowed &&
                                (rangedDefinitionIndex <= 0 ||
                                    current.rangedStowedSlot == 0 ||
                                    current.rangedStowed)
                            : current.rangedPresent &&
                                !current.rangedStowed &&
                                (meleeDefinitionIndex <= 0 ||
                                    current.meleeStowedSlot == 0 ||
                                    current.meleeStowed);
                const bool exactCarrySlots =
                    (meleeDefinitionIndex <= 0 ||
                        meleeAttachmentSlot == 0 ||
                        current.meleeAttachmentSlot ==
                            meleeAttachmentSlot) &&
                    (rangedDefinitionIndex <= 0 ||
                        rangedAttachmentSlot == 0 ||
                        current.rangedAttachmentSlot ==
                            rangedAttachmentSlot);
                const bool presentationReady = inspected &&
                    familyReady && exactCarrySlots &&
                    (meleeDefinitionIndex <= 0 ||
                        current.meleePresent) &&
                    (rangedDefinitionIndex <= 0 ||
                        current.rangedPresent);
                if (presentationReady)
                {
                    applied = true;
                }
                else
                {
                    // The remote entity owns the AI inventory stack rather
                    // than CTCHeroInventoryWeapons, so retail Hero sheathe
                    // actions cannot complete on it. Reproduce the final
                    // CTCCarrying mutation observed from the owning Hero.
                    // EquipWeapon can materialize a weapon without a
                    // CTCCarrying entry. Treat slot zero as an unattached
                    // weapon that still needs its first target attachment;
                    // do not call remove on it before that initial attach.
                    const bool moveMelee = inspected &&
                        meleeWeapon != nullptr && targetMeleeSlot != 0 &&
                        current.meleeAttachmentSlot != targetMeleeSlot;
                    const bool moveRanged = inspected &&
                        rangedWeapon != nullptr && targetRangedSlot != 0 &&
                        current.rangedAttachmentSlot != targetRangedSlot;
                    if (moveMelee)
                    {
                        if (current.meleeAttachmentSlot != 0)
                        {
                            functions.remove(carrying, meleeWeapon);
                        }
                        functions.attach(
                            carrying,
                            meleeWeapon,
                            targetMeleeSlot,
                            true);
                    }
                    if (moveRanged)
                    {
                        if (current.rangedAttachmentSlot != 0)
                        {
                            functions.remove(carrying, rangedWeapon);
                        }
                        functions.attach(
                            carrying,
                            rangedWeapon,
                            targetRangedSlot,
                            true);
                    }
                    CreatureWeaponInspection verified;
                    const bool verifiedReadable = Inspect(
                        creature,
                        meleeDefinitionIndex,
                        rangedDefinitionIndex,
                        verified);
                    const bool verifiedFamily =
                        activeFamily == CreatureWeaponFamily::None
                            ? (meleeDefinitionIndex <= 0 ||
                                    verified.meleeStowedSlot == 0 ||
                                    verified.meleeStowed) &&
                                (rangedDefinitionIndex <= 0 ||
                                    verified.rangedStowedSlot == 0 ||
                                    verified.rangedStowed)
                            : activeFamily == CreatureWeaponFamily::Melee
                                ? verified.meleePresent &&
                                    !verified.meleeStowed &&
                                    (rangedDefinitionIndex <= 0 ||
                                        verified.rangedStowedSlot == 0 ||
                                        verified.rangedStowed)
                                : verified.rangedPresent &&
                                    !verified.rangedStowed &&
                                    (meleeDefinitionIndex <= 0 ||
                                        verified.meleeStowedSlot == 0 ||
                                        verified.meleeStowed);
                    const bool verifiedCarrySlots =
                        (meleeDefinitionIndex <= 0 ||
                            meleeAttachmentSlot == 0 ||
                            verified.meleeAttachmentSlot ==
                                meleeAttachmentSlot) &&
                        (rangedDefinitionIndex <= 0 ||
                            rangedAttachmentSlot == 0 ||
                            verified.rangedAttachmentSlot ==
                                rangedAttachmentSlot);
                    applied = verifiedReadable && verifiedFamily &&
                        verifiedCarrySlots &&
                        (meleeDefinitionIndex <= 0 ||
                            verified.meleePresent) &&
                        (rangedDefinitionIndex <= 0 ||
                            verified.rangedPresent);
                    if (!applied && (moveMelee || moveRanged))
                    {
                        // A two-weapon presentation update is one logical
                        // mutation. Restore both original carry entries if
                        // verification fails so retries never inherit a
                        // partially detached loadout.
                        if (moveMelee)
                        {
                            functions.remove(carrying, meleeWeapon);
                            if (current.meleeAttachmentSlot != 0)
                            {
                                functions.attach(
                                    carrying,
                                    meleeWeapon,
                                    current.meleeAttachmentSlot,
                                    true);
                            }
                        }
                        if (moveRanged)
                        {
                            functions.remove(carrying, rangedWeapon);
                            if (current.rangedAttachmentSlot != 0)
                            {
                                functions.attach(
                                    carrying,
                                    rangedWeapon,
                                    current.rangedAttachmentSlot,
                                    true);
                            }
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }

        if (inspection != nullptr)
        {
            const bool inspected = Inspect(
                creature,
                meleeDefinitionIndex,
                rangedDefinitionIndex,
                *inspection);
            (void)inspected;
        }
        return applied;
    }
}
