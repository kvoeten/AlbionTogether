#include "RemoteHeroRuntimeDefinition.h"

#include "Game/Native/Addresses.h"
#include "Game/Native/ScriptTypes.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    using fable::game::native::CharString;

    constexpr std::size_t kCreatureDefinitionSize = 0x160;
    constexpr std::size_t kReferenceCountOffset = 0x04;
    constexpr std::size_t kMeshDimensionsOffset = 0x7C;
    constexpr std::size_t kMeshDimensionsSize = 0x10;
    constexpr std::size_t kGraphicOffset = 0x108;
    constexpr std::size_t kGraphicSize = 0x10;
    constexpr std::size_t kEyeGraphicOffset = 0x11C;
    constexpr std::size_t kEyeGraphicSize = 0x10;
    constexpr std::size_t kCopyVtableSlot = 0x4C / sizeof(void*);
    constexpr std::size_t kAddSubDefVtableSlot = 0x30 / sizeof(void*);
    constexpr std::size_t kRemoveSubDefVtableSlot = 0x34 / sizeof(void*);
    constexpr std::size_t kGetSubDefVtableSlot = 0x38 / sizeof(void*);

    constexpr char kHeroDefinitionName[] = "CREATURE_HERO";
    constexpr char kRivalDefinitionName[] =
        "CREATURE_HERO_RIVAL_GOOD_01";

    constexpr std::array<const char*, 5> kHeroSubDefinitions = {
        "CAppearanceDef",
        "CCarryingDef",
        "CHeroMorphDef",
        "CEntitySoundDef",
        "CCreatureDef",
    };

    struct SubDefinitionInfo final
    {
        int definitionIndex = 0;
        int ownerIndex = 0;
    };

    using CharStringConstructor = CharString* (__thiscall*)(
        CharString*, const char*, int);
    using CharStringDestructor = void(__thiscall*)(CharString*);
    using DefinitionIndexByName = unsigned int(__thiscall*)(
        void*, const CharString*);
    using DefinitionConstructor = void* (__thiscall*)(void*);
    using DefinitionCopy = void(__thiscall*)(void*, const void*);
    using AddSubDefinition = void(__thiscall*)(
        void*, const CharString*, int, int);
    using RemoveSubDefinition = void(__thiscall*)(
        void*, const CharString*);
    using GetSubDefinition = SubDefinitionInfo* (__thiscall*)(
        void*, const CharString*);
    using ScalarDeletingDestructor = void* (__thiscall*)(void*, unsigned int);
    using ReleaseDefinition = void(__thiscall*)(void*);

    bool ReleaseReference(void*& definition) noexcept
    {
        if (definition == nullptr)
        {
            return true;
        }
        bool released = false;
        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(definition);
            auto& references = *reinterpret_cast<unsigned int*>(
                bytes + kReferenceCountOffset);
            if (references > 0)
            {
                --references;
                if (references == 0)
                {
                    auto** const vtable = *reinterpret_cast<void***>(definition);
                    reinterpret_cast<ReleaseDefinition>(vtable[1])(definition);
                }
                released = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            released = false;
        }
        definition = nullptr;
        return released;
    }

    bool DestroyUnpublishedDefinition(void* definition) noexcept
    {
        if (definition == nullptr)
        {
            return true;
        }
        bool destroyed = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(definition);
            reinterpret_cast<ScalarDeletingDestructor>(vtable[0])(
                definition, 0);
            destroyed = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            destroyed = false;
        }
        if (destroyed)
        {
            VirtualFree(definition, 0, MEM_RELEASE);
        }
        return destroyed;
    }
}

namespace fable::game::hero_pawn::appearance::native
{
    static_assert(sizeof(void*) == 4);

    bool RemoteHeroRuntimeDefinition::Ensure(
        HMODULE gameModule,
        DefinitionLookup definitionLookup,
        const core::Diagnostics& diagnostics) noexcept
    {
        if (definition_ != nullptr)
        {
            return true;
        }
        if (gameModule == nullptr || definitionLookup == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        void* hero = nullptr;
        void* rival = nullptr;
        void* clone = nullptr;
        void* heroVtable = nullptr;
        void* rivalVtable = nullptr;
        const char* failureStage = "definition-manager";
        unsigned int heroIndex = 0;
        unsigned int rivalIndex = 0;
        int sourceSubDefinitionIndex = 0;
        int sourceSubDefinitionOwner = 0;
        int installedSubDefinitionIndex = 0;
        int installedSubDefinitionOwner = 0;
        bool ready = false;
        __try
        {
            void* const thingManager = *reinterpret_cast<void* const*>(
                base + game::native::rva::ThingManagerSlot);
            void* const definitionManager = thingManager != nullptr
                ? *reinterpret_cast<void* const*>(
                    static_cast<std::uint8_t*>(thingManager) + 0x2C)
                : nullptr;
            if (definitionManager == nullptr)
            {
                __leave;
            }
            failureStage = "definition-indices";

            const auto constructString =
                reinterpret_cast<CharStringConstructor>(
                    base + game::native::rva::CharStringConstructor);
            const auto destroyString =
                reinterpret_cast<CharStringDestructor>(
                    base + game::native::rva::CharStringDestructor);
            const auto resolveIndex =
                reinterpret_cast<DefinitionIndexByName>(
                    base + game::native::rva::DefinitionIndexByName);

            CharString heroName;
            constructString(&heroName, kHeroDefinitionName, -1);
            heroIndex = resolveIndex(definitionManager, &heroName);
            destroyString(&heroName);
            CharString rivalName;
            constructString(&rivalName, kRivalDefinitionName, -1);
            rivalIndex = resolveIndex(definitionManager, &rivalName);
            destroyString(&rivalName);
            if (heroIndex == 0 || rivalIndex == 0)
            {
                __leave;
            }

            failureStage = "definition-lookup";
            if (!definitionLookup(definitionManager, heroIndex, &hero) ||
                !definitionLookup(definitionManager, rivalIndex, &rival) ||
                hero == nullptr || rival == nullptr)
            {
                __leave;
            }

            void** const expectedVtable = reinterpret_cast<void**>(
                base + game::native::rva::ThingCreatureDefinitionVtable);
            heroVtable = *reinterpret_cast<void**>(hero);
            rivalVtable = *reinterpret_cast<void**>(rival);
            failureStage = "definition-type";
            if (heroVtable != expectedVtable ||
                rivalVtable != expectedVtable)
            {
                __leave;
            }

            failureStage = "definition-allocation";
            clone = VirtualAlloc(
                nullptr,
                kCreatureDefinitionSize,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE);
            if (clone == nullptr)
            {
                __leave;
            }

            failureStage = "definition-construction";
            const auto constructDefinition =
                reinterpret_cast<DefinitionConstructor>(
                    base + game::native::rva::
                        ThingCreatureDefinitionConstructor);
            if (constructDefinition(clone) != clone ||
                *reinterpret_cast<void***>(clone) != expectedVtable)
            {
                __leave;
            }

            failureStage = "definition-copy";
            auto** const vtable = *reinterpret_cast<void***>(clone);
            reinterpret_cast<DefinitionCopy>(vtable[kCopyVtableSlot])(
                clone, rival);
            const auto addSubDefinition =
                reinterpret_cast<AddSubDefinition>(
                    vtable[kAddSubDefVtableSlot]);
            const auto removeSubDefinition =
                reinterpret_cast<RemoveSubDefinition>(
                    vtable[kRemoveSubDefVtableSlot]);
            const auto getHeroSubDefinition =
                reinterpret_cast<GetSubDefinition>(
                    (*reinterpret_cast<void***>(hero))[kGetSubDefVtableSlot]);
            const auto getCloneSubDefinition =
                reinterpret_cast<GetSubDefinition>(
                    vtable[kGetSubDefVtableSlot]);

            for (const char* const name : kHeroSubDefinitions)
            {
                failureStage = name;
                CharString value;
                constructString(&value, name, -1);
                const SubDefinitionInfo* const source =
                    getHeroSubDefinition(hero, &value);
                if (source != nullptr)
                {
                    sourceSubDefinitionIndex = source->definitionIndex;
                    sourceSubDefinitionOwner = source->ownerIndex;
                }
                if (source == nullptr || sourceSubDefinitionIndex <= 0)
                {
                    failureStage = "subdef-source";
                    destroyString(&value);
                    __leave;
                }

                // AddSubDef only inserts. Remove the rival entry from this
                // private clone before inserting the Hero entry.
                removeSubDefinition(clone, &value);
                addSubDefinition(
                    clone,
                    &value,
                    sourceSubDefinitionIndex,
                    static_cast<int>(rivalIndex));
                const SubDefinitionInfo* const installed =
                    getCloneSubDefinition(clone, &value);
                if (installed != nullptr)
                {
                    installedSubDefinitionIndex = installed->definitionIndex;
                    installedSubDefinitionOwner = installed->ownerIndex;
                }
                const bool valid = installed != nullptr &&
                    installedSubDefinitionIndex == sourceSubDefinitionIndex &&
                    installedSubDefinitionOwner == static_cast<int>(rivalIndex);
                destroyString(&value);
                if (!valid)
                {
                    failureStage = "subdef-install";
                    __leave;
                }
            }

            failureStage = "definition-presentation-fields";
            auto* const cloneBytes = static_cast<std::uint8_t*>(clone);
            const auto* const heroBytes =
                static_cast<const std::uint8_t*>(hero);
            std::memcpy(
                cloneBytes + kMeshDimensionsOffset,
                heroBytes + kMeshDimensionsOffset,
                kMeshDimensionsSize);
            std::memcpy(
                cloneBytes + kGraphicOffset,
                heroBytes + kGraphicOffset,
                kGraphicSize);
            std::memcpy(
                cloneBytes + kEyeGraphicOffset,
                heroBytes + kEyeGraphicOffset,
                kEyeGraphicSize);

            definition_ = clone;
            retailBaseIndex_ = rivalIndex;
            clone = nullptr;
            ready = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ready = false;
        }

        (void)ReleaseReference(hero);
        (void)ReleaseReference(rival);
        if (clone != nullptr)
        {
            (void)DestroyUnpublishedDefinition(clone);
        }
        if (!ready)
        {
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "stage=%s hero_index=%u rival_index=%u hero_vtable=%p rival_vtable=%p source=%d/%d installed=%d/%d",
                failureStage,
                heroIndex,
                rivalIndex,
                heroVtable,
                rivalVtable,
                sourceSubDefinitionIndex,
                sourceSubDefinitionOwner,
                installedSubDefinitionIndex,
                installedSubDefinitionOwner);
            diagnostics.Log(detail);
            diagnostics.Event(
                "ClientFailed",
                detail);
            return false;
        }

        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "definition=%p retail_base=%s hero_source=%s rival_index=%u subdefs=%zu",
            definition_,
            kRivalDefinitionName,
            kHeroDefinitionName,
            retailBaseIndex_,
            kHeroSubDefinitions.size());
        diagnostics.Event(
            "MultiplayerRemoteHeroRuntimeDefinitionBuilt",
            detail);
        return true;
    }

    bool RemoteHeroRuntimeDefinition::ReplaceReference(void** result) noexcept
    {
        if (result == nullptr || definition_ == nullptr)
        {
            return false;
        }
        bool replaced = false;
        __try
        {
            void* previous = *result;
            if (previous != definition_)
            {
                auto* const bytes = static_cast<std::uint8_t*>(definition_);
                ++*reinterpret_cast<unsigned int*>(
                    bytes + kReferenceCountOffset);
                *result = definition_;
                (void)ReleaseReference(previous);
            }
            replaced = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            replaced = false;
        }
        return replaced;
    }

    void* RemoteHeroRuntimeDefinition::Get() const noexcept
    {
        return definition_;
    }

    unsigned int RemoteHeroRuntimeDefinition::RetailBaseIndex() const noexcept
    {
        return retailBaseIndex_;
    }

    void RemoteHeroRuntimeDefinition::AbandonForProcessLifetime() noexcept
    {
        definition_ = nullptr;
        retailBaseIndex_ = 0;
    }
}
