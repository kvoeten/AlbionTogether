#pragma once

#include <Windows.h>

#include <cstdint>

namespace fable::game::creature::equipment::native::detail
{
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
    using InitializePointer = void(__thiscall*)(void* pointer);
    using AssignPointer = void(__thiscall*)(void* pointer, void* thing);
    using DestroyPointer = void(__thiscall*)(void* pointer);
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
        InitializePointer initializePointer = nullptr;
        AssignPointer assignPointer = nullptr;
        DestroyPointer destroyPointer = nullptr;
        GetDefinition getDefinition = nullptr;
        ResolveWeaponProperties resolveProperties = nullptr;
        AttachWeapon attach = nullptr;
        RemoveWeapon remove = nullptr;
        void** attachmentSlotRegistry = nullptr;
        ResolveAttachmentSlot resolveAttachmentSlot = nullptr;
    };

    inline constexpr std::uint32_t CoreFunctionMask = 0x07FFu;
    inline constexpr std::uint32_t PointerLifecycleFunctionMask = 0x3800u;

    bool ResolveFunctions(
        HMODULE gameModule,
        Functions& functions,
        std::uint32_t* signatureMask = nullptr) noexcept;
    bool IsSaneDefinition(std::int32_t definitionIndex) noexcept;
    void* FindCarrying(void* creature) noexcept;
    bool Contains(
        const Functions& functions,
        void* carrying,
        std::int32_t definitionIndex) noexcept;
    void ReadWeaponPresentation(void* weapon, void*& graphic) noexcept;
    void ReadThingDefinition(
        void* thing,
        std::int32_t& definitionIndex) noexcept;
    std::uint64_t ReadThingUid(void* thing) noexcept;
    void ReleaseReference(void* reference) noexcept;
    bool AttachmentSlotAvailable(
        const Functions& functions,
        std::uint32_t slot) noexcept;
    bool ReadAttachmentSlot(
        const Functions& functions,
        void* carrying,
        void* weapon,
        std::uint32_t& slot) noexcept;
}
