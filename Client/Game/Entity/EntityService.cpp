#include "EntityService.h"

#include "Entity.h"
#include "Game/Creature/Control/ScriptControl.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "../Native/Addresses.h"

#include <cstring>
#include <cstdio>

namespace
{
    using namespace fable::game;
    using namespace fable::game::native;

    using CharStringConstructor = void(__thiscall*)(CharString*, const char*, int);
    using CharStringDestructor = void(__thiscall*)(CharString*);
    using FindThing = ScriptThing* (__thiscall*)(
        GameScriptInterface*, ScriptThing*, const CharString*);
    using CreateCreatureFunction = ScriptThing* (__thiscall*)(
        GameScriptInterface*,
        ScriptThing*,
        const CharString*,
        const Vector3*,
        const CharString*,
        bool);
    using ScriptThingDestructor = void* (__thiscall*)(ScriptThing*, unsigned int);
    using ScriptThingPredicate = bool(__thiscall*)(const ScriptThing*);
    using ScriptThingSetter = void(__thiscall*)(const ScriptThing*, bool);
    using ScriptThingVoid = void(__thiscall*)(const ScriptThing*);
    using ScriptThingInteger = int(__thiscall*)(const ScriptThing*);
    using ScriptThingGetBorrowedString = const CharString* (__thiscall*)(const ScriptThing*);
    using ScriptThingGetOwnedString = CharString* (__thiscall*)(const ScriptThing*, CharString*);
    using ScriptThingSetString = void(__thiscall*)(const ScriptThing*, const CharString*);
    using GetPosition = const Vector3* (__thiscall*)(const ScriptThing*);
    using GetFacing = float(__thiscall*)(const ScriptThing*);
    using ResolveNativeThing = void* (__thiscall*)(const ScriptThing*);
    using RequestDestroyFunction = void(__thiscall*)(void*, bool);
    using SetFlagFunction = void(__thiscall*)(GameScriptInterface*, const ScriptThing*, bool);
    using TeleportFunction = void(__thiscall*)(
        GameScriptInterface*, const ScriptThing*, const Vector3*, float, bool, int);
    using AttackFunction = void(__thiscall*)(
        GameScriptInterface*, const ScriptThing*, const ScriptThing*, bool, bool);
    using StartControlFunction = bool(__thiscall*)(
        GameScriptInterface*,
        const ScriptThing*,
        ScriptControlHandle*,
        int);
    using ControlDeleteFunction = void(__cdecl*)(void*);
    using GameHeapFreeFunction = void(__cdecl*)(void*);

    bool CopyCharString(const CharString& source, std::string& destination)
    {
        constexpr std::size_t kMaximumMetadataLength = 4096;
        char buffer[kMaximumMetadataLength + 1] = {};
        std::size_t length = 0;
        bool valid = true;
        __try
        {
            if (source.stringData != nullptr)
            {
                const auto stringData = static_cast<const unsigned char*>(source.stringData);
                const char* text = *reinterpret_cast<const char* const*>(stringData + sizeof(void*));
                if (text != nullptr)
                {
                    while (length < kMaximumMetadataLength && text[length] != '\0')
                    {
                        buffer[length] = text[length];
                        ++length;
                    }
                    valid = length < kMaximumMetadataLength || text[length] == '\0';
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            destination.clear();
            return false;
        }
        destination.assign(buffer, length);
        return true;
    }
}

namespace fable::game
{
    bool EntityService::Initialize(HMODULE gameModule, const core::Diagnostics& diagnostics)
    {
        gameModule_ = gameModule;
        diagnostics_ = diagnostics;
        staticApiValidated_ = false;
        metadataApiValidated_ = false;
        interactionApiValidated_ = false;
        lifecycleApiValidated_ = false;
        if (gameModule_ == nullptr)
        {
            diagnostics_.Log("Entity API: game module is unavailable.");
            return false;
        }
        if (!interfaceAccess_.Initialize(gameModule_, diagnostics_))
        {
            diagnostics_.Log("Entity API: native game interface access initialization failed.");
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        const auto expectedScriptThingVtable = reinterpret_cast<void**>(
            base + native::rva::ScriptThingVtable);
        __try
        {
            staticApiValidated_ =
                expectedScriptThingVtable[native::script_thing_slot::Destructor] ==
                    reinterpret_cast<void*>(base + native::rva::ScriptThingDestructor) &&
                expectedScriptThingVtable[native::script_thing_slot::GetPosition] ==
                    reinterpret_cast<void*>(base + native::rva::ScriptThingGetPosition) &&
                expectedScriptThingVtable[native::script_thing_slot::GetFacing] ==
                    reinterpret_cast<void*>(base + native::rva::ScriptThingGetFacing) &&
                expectedScriptThingVtable[native::script_thing_slot::IsNull] ==
                    reinterpret_cast<void*>(base + native::rva::ScriptThingIsNull);
            metadataApiValidated_ = staticApiValidated_ &&
                expectedScriptThingVtable[native::script_thing_slot::GetName] == reinterpret_cast<void*>(base + native::rva::ScriptThingGetName) &&
                expectedScriptThingVtable[native::script_thing_slot::GetDefinitionName] == reinterpret_cast<void*>(base + native::rva::ScriptThingGetDefinitionName) &&
                expectedScriptThingVtable[native::script_thing_slot::GetDataString] == reinterpret_cast<void*>(base + native::rva::ScriptThingGetDataString) &&
                expectedScriptThingVtable[native::script_thing_slot::SetDataString] == reinterpret_cast<void*>(base + native::rva::ScriptThingSetDataString) &&
                expectedScriptThingVtable[native::script_thing_slot::GetCurrentMapName] == reinterpret_cast<void*>(base + native::rva::ScriptThingGetCurrentMapName) &&
                expectedScriptThingVtable[native::script_thing_slot::GetHomeMapName] == reinterpret_cast<void*>(base + native::rva::ScriptThingGetHomeMapName);
            interactionApiValidated_ = staticApiValidated_ &&
                expectedScriptThingVtable[native::script_thing_slot::IsSneaking] == reinterpret_cast<void*>(base + native::rva::ScriptThingIsSneaking) &&
                expectedScriptThingVtable[native::script_thing_slot::IsAwareOfHero] == reinterpret_cast<void*>(base + native::rva::ScriptThingIsAwareOfHero) &&
                expectedScriptThingVtable[native::script_thing_slot::IsUnconscious] == reinterpret_cast<void*>(base + native::rva::ScriptThingIsUnconscious) &&
                expectedScriptThingVtable[native::script_thing_slot::IsUsable] == reinterpret_cast<void*>(base + native::rva::ScriptThingIsUsable) &&
                expectedScriptThingVtable[native::script_thing_slot::IsOpenDoor] == reinterpret_cast<void*>(base + native::rva::ScriptThingIsOpenDoor) &&
                expectedScriptThingVtable[native::script_thing_slot::IsSummonedCreature] == reinterpret_cast<void*>(base + native::rva::ScriptThingIsSummonedCreature) &&
                expectedScriptThingVtable[native::script_thing_slot::SetAsUsable] == reinterpret_cast<void*>(base + native::rva::ScriptThingSetAsUsable) &&
                expectedScriptThingVtable[native::script_thing_slot::SetFriendsWithEverything] == reinterpret_cast<void*>(base + native::rva::ScriptThingSetFriendsWithEverything) &&
                expectedScriptThingVtable[native::script_thing_slot::GetActivationTriggerStatus] == reinterpret_cast<void*>(base + native::rva::ScriptThingGetActivationTriggerStatus) &&
                expectedScriptThingVtable[native::script_thing_slot::SetActivationTriggerStatus] == reinterpret_cast<void*>(base + native::rva::ScriptThingSetActivationTriggerStatus) &&
                expectedScriptThingVtable[native::script_thing_slot::SetToKillOnLevelUnload] == reinterpret_cast<void*>(base + native::rva::ScriptThingSetToKillOnLevelUnload) &&
                expectedScriptThingVtable[native::script_thing_slot::UpdateAttachment] == reinterpret_cast<void*>(base + native::rva::ScriptThingUpdateAttachment) &&
                expectedScriptThingVtable[native::script_thing_slot::IncrementScriptCounter] == reinterpret_cast<void*>(base + native::rva::ScriptThingIncrementScriptCounter) &&
                expectedScriptThingVtable[native::script_thing_slot::DecrementScriptCounter] == reinterpret_cast<void*>(base + native::rva::ScriptThingDecrementScriptCounter) &&
                expectedScriptThingVtable[native::script_thing_slot::GetScriptCounter] == reinterpret_cast<void*>(base + native::rva::ScriptThingGetScriptCounter);
            constexpr unsigned char expectedRequestDestroy[] = {
                0x56, 0x8B, 0xF1, 0x8A, 0x86, 0x9D, 0x00, 0x00, 0x00};
            lifecycleApiValidated_ = staticApiValidated_ &&
                std::memcmp(
                    reinterpret_cast<const void*>(
                        base + native::rva::ThingRequestDestroy),
                    expectedRequestDestroy,
                    sizeof(expectedRequestDestroy)) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            staticApiValidated_ = false;
            metadataApiValidated_ = false;
            interactionApiValidated_ = false;
            lifecycleApiValidated_ = false;
        }

        diagnostics_.Log(staticApiValidated_
            ? "Entity API: CScriptThing ABI validated."
            : "Entity API: CScriptThing ABI validation failed.");
        diagnostics_.Log(interactionApiValidated_
            ? "Entity API: CScriptThing interaction-state ABI validated."
            : "Entity API: CScriptThing interaction-state ABI unavailable.");
        diagnostics_.Log(metadataApiValidated_
            ? "Entity API: CScriptThing metadata ABI validated."
            : "Entity API: CScriptThing metadata ABI unavailable.");
        diagnostics_.Log(lifecycleApiValidated_
            ? "Entity API: native CThing lifecycle ABI validated."
            : "Entity API: native CThing lifecycle ABI unavailable.");
        return staticApiValidated_;
    }

    native::GameScriptInterface* EntityService::ResolveInterface() const
    {
        return staticApiValidated_ ? interfaceAccess_.Resolve() : nullptr;
    }

    Entity* EntityService::GetHero()
    {
        return FindByScriptNameNative("SCRIPT_NAME_HERO");
    }

    Entity* EntityService::FindByScriptName(const std::string& scriptName)
    {
        return FindByScriptNameNative(scriptName.c_str());
    }

    Entity* EntityService::FindByScriptNameNative(const char* scriptName)
    {
        native::ScriptThing result;
        if (!FindByScriptNameHandleNative(scriptName, result))
        {
            return nullptr;
        }
        return new Entity(*this, result);
    }

    bool EntityService::FindByScriptNameHandleNative(
        const char* scriptName,
        native::ScriptThing& result)
    {
        result = {};
        native::GameScriptInterface* gameInterface = ResolveInterface();
        if (gameInterface == nullptr || scriptName == nullptr || scriptName[0] == '\0')
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + native::rva::CharStringDestructor);
        const auto findThing = reinterpret_cast<FindThing>(
            gameInterface->vtable[native::game_interface_slot::GetThingWithScriptName]);
        native::CharString name;
        bool constructed = false;
        bool valid = false;
        __try
        {
            constructString(&name, scriptName, -1);
            constructed = true;
            valid = findThing(gameInterface, &result, &name) == &result && IsValid(result);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (constructed)
        {
            __try
            {
                destroyString(&name);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                valid = false;
            }
        }
        if (!valid)
        {
            ReleaseHandle(result);
            return false;
        }
        return true;
    }

    Entity* EntityService::CreateCreature(
        const std::string& definition,
        const Vector3& position,
        const std::string& scriptName)
    {
        return CreateCreatureNative(definition.c_str(), position, scriptName.c_str());
    }

    Entity* EntityService::CreateCreatureNative(
        const char* definition,
        const Vector3& position,
        const char* scriptName)
    {
        native::ScriptThing result;
        if (!CreateCreatureHandleNative(definition, position, scriptName, result))
        {
            return nullptr;
        }
        void* const nativeThing = ResolveNative(result);
        char detail[384] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "definition=%s native=%p implementation=%p pointer_info=%p",
            definition,
            nativeThing,
            result.implementation,
            result.pointerInfo);
        diagnostics_.Event("ScriptCreatureCreated", detail);
        return new Entity(*this, result);
    }

    bool EntityService::CreateCreatureHandleNative(
        const char* definition,
        const Vector3& position,
        const char* scriptName,
        native::ScriptThing& result)
    {
        result = {};
        native::GameScriptInterface* gameInterface = ResolveInterface();
        if (gameInterface == nullptr || definition == nullptr || definition[0] == '\0')
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + native::rva::CharStringDestructor);
        const auto createCreature = reinterpret_cast<CreateCreatureFunction>(
            gameInterface->vtable[native::game_interface_slot::CreateCreature]);
        native::CharString definitionString;
        native::CharString scriptNameString;
        bool definitionConstructed = false;
        bool scriptNameConstructed = false;
        bool valid = false;
        __try
        {
            constructString(&definitionString, definition, -1);
            definitionConstructed = true;
            constructString(&scriptNameString, scriptName != nullptr ? scriptName : "", -1);
            scriptNameConstructed = true;
            valid = createCreature(
                gameInterface,
                &result,
                &definitionString,
                &position,
                &scriptNameString,
                false) == &result && IsValid(result);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }

        if (scriptNameConstructed)
        {
            __try { destroyString(&scriptNameString); }
            __except (EXCEPTION_EXECUTE_HANDLER) { valid = false; }
        }
        if (definitionConstructed)
        {
            __try { destroyString(&definitionString); }
            __except (EXCEPTION_EXECUTE_HANDLER) { valid = false; }
        }
        if (!valid)
        {
            diagnostics_.Log("Entity API: CreateCreature failed.");
            ReleaseHandle(result);
            return false;
        }
        return true;
    }

    bool EntityService::IsValid(const native::ScriptThing& handle) const
    {
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        if (!staticApiValidated_ ||
            handle.vtable != reinterpret_cast<void**>(base + native::rva::ScriptThingVtable))
        {
            return false;
        }
        return !CallScriptThingPredicate(handle, native::script_thing_slot::IsNull, true);
    }

    bool EntityService::IsAlive(const native::ScriptThing& handle) const
    {
        return IsValid(handle) &&
            CallScriptThingPredicate(handle, native::script_thing_slot::IsAlive, false);
    }

    bool EntityService::IsDead(const native::ScriptThing& handle) const
    {
        return !IsValid(handle) ||
            CallScriptThingPredicate(handle, native::script_thing_slot::IsDead, true);
    }

    bool EntityService::IsSneaking(const native::ScriptThing& handle) const
    {
        return interactionApiValidated_ && CallScriptThingPredicate(handle, native::script_thing_slot::IsSneaking, false);
    }

    bool EntityService::IsAwareOfHero(const native::ScriptThing& handle) const
    {
        return interactionApiValidated_ && CallScriptThingPredicate(handle, native::script_thing_slot::IsAwareOfHero, false);
    }

    bool EntityService::IsUnconscious(const native::ScriptThing& handle) const
    {
        return interactionApiValidated_ && CallScriptThingPredicate(handle, native::script_thing_slot::IsUnconscious, false);
    }

    bool EntityService::IsUsable(const native::ScriptThing& handle) const
    {
        return interactionApiValidated_ && CallScriptThingPredicate(handle, native::script_thing_slot::IsUsable, false);
    }

    bool EntityService::IsOpenDoor(const native::ScriptThing& handle) const
    {
        return interactionApiValidated_ && CallScriptThingPredicate(handle, native::script_thing_slot::IsOpenDoor, false);
    }

    bool EntityService::IsSummonedCreature(const native::ScriptThing& handle) const
    {
        return interactionApiValidated_ && CallScriptThingPredicate(handle, native::script_thing_slot::IsSummonedCreature, false);
    }

    std::string EntityService::GetName(const native::ScriptThing& handle) const
    {
        std::string value;
        ReadBorrowedString(handle, native::script_thing_slot::GetName, value);
        return value;
    }

    std::string EntityService::GetDefinitionName(const native::ScriptThing& handle) const
    {
        std::string value;
        ReadOwnedString(handle, native::script_thing_slot::GetDefinitionName, value);
        return value;
    }

    std::string EntityService::GetDataString(const native::ScriptThing& handle) const
    {
        std::string value;
        ReadOwnedString(handle, native::script_thing_slot::GetDataString, value);
        return value;
    }

    std::string EntityService::GetCurrentMapName(const native::ScriptThing& handle) const
    {
        std::string value;
        ReadOwnedString(handle, native::script_thing_slot::GetCurrentMapName, value);
        return value;
    }

    std::string EntityService::GetHomeMapName(const native::ScriptThing& handle) const
    {
        std::string value;
        ReadOwnedString(handle, native::script_thing_slot::GetHomeMapName, value);
        return value;
    }

    bool EntityService::SetDataString(const native::ScriptThing& handle, const std::string& value)
    {
        if (!metadataApiValidated_ || !IsValid(handle))
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + native::rva::CharStringDestructor);
        native::CharString nativeValue;
        bool constructed = false;
        bool written = false;
        __try
        {
            constructString(&nativeValue, value.c_str(), -1);
            constructed = true;
            const auto setter = reinterpret_cast<ScriptThingSetString>(
                handle.vtable[native::script_thing_slot::SetDataString]);
            setter(&handle, &nativeValue);
            written = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            written = false;
        }
        if (constructed)
        {
            __try { destroyString(&nativeValue); }
            __except (EXCEPTION_EXECUTE_HANDLER) { written = false; }
        }
        return written;
    }

    bool EntityService::GetActivationTriggerStatus(const native::ScriptThing& handle) const
    {
        if (!interactionApiValidated_)
        {
            return false;
        }
        void* nativeThing = ResolveNative(handle);
        if (!entity::native::ThingComponentAccess::Has(
                nativeThing,
                entity::native::ThingComponentType::ActivationTrigger))
        {
            return false;
        }
        return CallScriptThingPredicate(
            handle,
            native::script_thing_slot::GetActivationTriggerStatus,
            false);
    }

    int EntityService::GetScriptCounter(const native::ScriptThing& handle) const
    {
        if (!interactionApiValidated_ || !IsValid(handle))
        {
            return 0;
        }
        int value = 0;
        __try
        {
            value = reinterpret_cast<ScriptThingInteger>(handle.vtable[native::script_thing_slot::GetScriptCounter])(&handle);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            value = 0;
        }
        return value;
    }

    bool EntityService::CallScriptThingSetter(const native::ScriptThing& handle, std::size_t vtableIndex, bool value) const
    {
        if (!interactionApiValidated_ || !IsValid(handle))
        {
            return false;
        }
        __try
        {
            reinterpret_cast<ScriptThingSetter>(handle.vtable[vtableIndex])(&handle, value);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool EntityService::CallScriptThingVoid(const native::ScriptThing& handle, std::size_t vtableIndex) const
    {
        if (!interactionApiValidated_ || !IsValid(handle))
        {
            return false;
        }
        __try
        {
            reinterpret_cast<ScriptThingVoid>(handle.vtable[vtableIndex])(&handle);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool EntityService::ReadBorrowedString(
        const native::ScriptThing& handle,
        std::size_t vtableIndex,
        std::string& value) const
    {
        if (!metadataApiValidated_ || !IsValid(handle))
        {
            value.clear();
            return false;
        }
        const native::CharString* nativeValue = nullptr;
        __try
        {
            const auto getter = reinterpret_cast<ScriptThingGetBorrowedString>(
                handle.vtable[vtableIndex]);
            nativeValue = getter(&handle);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            nativeValue = nullptr;
        }
        return nativeValue != nullptr && CopyCharString(*nativeValue, value);
    }

    bool EntityService::ReadOwnedString(
        const native::ScriptThing& handle,
        std::size_t vtableIndex,
        std::string& value) const
    {
        if (!metadataApiValidated_ || !IsValid(handle))
        {
            value.clear();
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + native::rva::CharStringDestructor);
        native::CharString nativeValue;
        bool shouldDestroy = true;
        bool read = false;
        __try
        {
            const auto getter = reinterpret_cast<ScriptThingGetOwnedString>(
                handle.vtable[vtableIndex]);
            read = getter(&handle, &nativeValue) == &nativeValue &&
                CopyCharString(nativeValue, value);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            read = false;
        }
        if (shouldDestroy)
        {
            __try { destroyString(&nativeValue); }
            __except (EXCEPTION_EXECUTE_HANDLER) { read = false; }
        }
        if (!read)
        {
            value.clear();
        }
        return read;
    }

    bool EntityService::SetUsable(const native::ScriptThing& handle, bool enabled)
    {
        return CallScriptThingSetter(handle, native::script_thing_slot::SetAsUsable, enabled);
    }

    bool EntityService::SetFriendsWithEverything(const native::ScriptThing& handle, bool enabled)
    {
        return CallScriptThingSetter(handle, native::script_thing_slot::SetFriendsWithEverything, enabled);
    }

    bool EntityService::SetActivationTriggerStatus(const native::ScriptThing& handle, bool enabled)
    {
        void* nativeThing = ResolveNative(handle);
        if (!entity::native::ThingComponentAccess::Has(
                nativeThing,
                entity::native::ThingComponentType::ActivationTrigger))
        {
            return false;
        }
        return CallScriptThingSetter(
            handle,
            native::script_thing_slot::SetActivationTriggerStatus,
            enabled);
    }

    bool EntityService::SetKillOnLevelUnload(const native::ScriptThing& handle, bool enabled)
    {
        return CallScriptThingSetter(handle, native::script_thing_slot::SetToKillOnLevelUnload, enabled);
    }

    bool EntityService::RequestDestroy(
        const native::ScriptThing& handle,
        bool immediate)
    {
        if (!lifecycleApiValidated_)
        {
            return false;
        }
        void* const nativeThing = ResolveNative(handle);
        if (nativeThing == nullptr)
        {
            return false;
        }
        bool requested = false;
        __try
        {
            const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
            reinterpret_cast<RequestDestroyFunction>(
                base + native::rva::ThingRequestDestroy)(
                    nativeThing,
                    immediate);
            requested = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            requested = false;
        }
        return requested;
    }

    bool EntityService::UpdateAttachment(const native::ScriptThing& handle)
    {
        return CallScriptThingVoid(handle, native::script_thing_slot::UpdateAttachment);
    }

    bool EntityService::IncrementScriptCounter(const native::ScriptThing& handle)
    {
        return CallScriptThingVoid(handle, native::script_thing_slot::IncrementScriptCounter);
    }

    bool EntityService::DecrementScriptCounter(const native::ScriptThing& handle)
    {
        return CallScriptThingVoid(handle, native::script_thing_slot::DecrementScriptCounter);
    }

    bool EntityService::CallScriptThingPredicate(
        const native::ScriptThing& handle,
        std::size_t vtableIndex,
        bool fallback) const
    {
        bool result = fallback;
        __try
        {
            const auto function = reinterpret_cast<ScriptThingPredicate>(
                handle.vtable[vtableIndex]);
            result = function(&handle);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            result = fallback;
        }
        return result;
    }

    bool EntityService::ReadPosition(
        const native::ScriptThing& handle,
        Vector3& position) const
    {
        if (!IsValid(handle))
        {
            return false;
        }
        bool read = false;
        __try
        {
            const auto function = reinterpret_cast<GetPosition>(
                handle.vtable[native::script_thing_slot::GetPosition]);
            const Vector3* value = function(&handle);
            if (value != nullptr)
            {
                position = *value;
                read = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            read = false;
        }
        return read;
    }

    bool EntityService::ReadFacing(const native::ScriptThing& handle, float& facing) const
    {
        if (!IsValid(handle))
        {
            return false;
        }
        bool read = false;
        __try
        {
            const auto function = reinterpret_cast<GetFacing>(
                handle.vtable[native::script_thing_slot::GetFacing]);
            facing = function(&handle);
            read = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            read = false;
        }
        return read;
    }

    void* EntityService::ResolveNative(const native::ScriptThing& handle) const
    {
        if (!IsValid(handle))
        {
            return nullptr;
        }
        void* nativeThing = nullptr;
        __try
        {
            const auto function = reinterpret_cast<ResolveNativeThing>(
                handle.vtable[native::script_thing_slot::ResolveNative]);
            nativeThing = function(&handle);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            nativeThing = nullptr;
        }
        return nativeThing;
    }

    bool EntityService::Teleport(
        const native::ScriptThing& handle,
        const Vector3& position,
        float facing,
        bool effect)
    {
        native::GameScriptInterface* gameInterface = ResolveInterface();
        if (gameInterface == nullptr || !IsValid(handle))
        {
            return false;
        }
        bool applied = false;
        __try
        {
            const auto function = reinterpret_cast<TeleportFunction>(
                gameInterface->vtable[native::game_interface_slot::TeleportThing]);
            function(gameInterface, &handle, &position, facing, effect, 0);
            applied = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }
        return applied;
    }

    bool EntityService::SetFlag(
        const native::ScriptThing& handle,
        std::size_t vtableIndex,
        bool enabled) const
    {
        native::GameScriptInterface* gameInterface = ResolveInterface();
        if (gameInterface == nullptr || !IsValid(handle))
        {
            return false;
        }
        bool applied = false;
        __try
        {
            const auto function = reinterpret_cast<SetFlagFunction>(
                gameInterface->vtable[vtableIndex]);
            function(gameInterface, &handle, enabled);
            applied = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }
        return applied;
    }

    bool EntityService::SetAttackable(const native::ScriptThing& handle, bool enabled)
    {
        return SetFlag(handle, native::game_interface_slot::SetAttackable, enabled);
    }

    bool EntityService::SetDamageable(const native::ScriptThing& handle, bool enabled)
    {
        return SetFlag(handle, native::game_interface_slot::SetDamageable, enabled);
    }

    bool EntityService::SetCollidable(const native::ScriptThing& handle, bool enabled)
    {
        return SetFlag(handle, native::game_interface_slot::SetCollidable, enabled);
    }

    bool EntityService::SetDrawable(const native::ScriptThing& handle, bool enabled)
    {
        return SetFlag(handle, native::game_interface_slot::SetDrawable, enabled);
    }

    bool EntityService::Attack(
        const native::ScriptThing& attacker,
        const native::ScriptThing& target,
        bool stopCurrentAction,
        bool unsheathe)
    {
        native::GameScriptInterface* gameInterface = ResolveInterface();
        if (gameInterface == nullptr || !IsValid(attacker) || !IsValid(target))
        {
            return false;
        }
        bool applied = false;
        __try
        {
            const auto function = reinterpret_cast<AttackFunction>(
                gameInterface->vtable[native::game_interface_slot::SetAttackImmediately]);
            function(gameInterface, &attacker, &target, stopCurrentAction, unsheathe);
            applied = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }
        return applied;
    }

    ScriptControl* EntityService::AcquireControl(
        const native::ScriptThing& entity,
        AiPriority priority)
    {
        return ScriptControl::Create(*this, entity, priority);
    }

    bool EntityService::StartControl(
        const native::ScriptThing& entity,
        AiPriority priority,
        native::ScriptControlHandle& handle)
    {
        handle = {};
        native::GameScriptInterface* gameInterface = ResolveInterface();
        if (gameInterface == nullptr || !IsValid(entity))
        {
            return false;
        }
        bool acquired = false;
        __try
        {
            const auto function = reinterpret_cast<StartControlFunction>(
                gameInterface->vtable[native::game_interface_slot::StartScriptingEntity]);
            acquired = function(
                gameInterface,
                &entity,
                &handle,
                static_cast<int>(priority));
            const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
            acquired = acquired &&
                handle.vtable == nullptr &&
                handle.baseData == nullptr &&
                handle.implementation != nullptr &&
                handle.pointerInfo != nullptr &&
                *reinterpret_cast<void**>(handle.implementation) ==
                    reinterpret_cast<void*>(
                        base + native::rva::ScriptedControlImplementationVtable) &&
                handle.pointerInfo->referenceCount > 0 &&
                handle.pointerInfo->data == handle.implementation &&
                handle.pointerInfo->deleteFunction == reinterpret_cast<void*>(
                    base + native::rva::ScriptedControlDeleteFunction);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            acquired = false;
        }
        if (acquired)
        {
            char detail[512] = {};
            __try
            {
                void* implementationVtable =
                    *reinterpret_cast<void**>(handle.implementation);
                std::snprintf(
                    detail,
                    std::size(detail),
                    "handle=%p handle_vtable=%p base_data=%p implementation=%p implementation_vtable=%p pointer_info=%p reference_count=%u delete_function=%p counted_data=%p priority=%d",
                    &handle,
                    handle.vtable,
                    handle.baseData,
                    handle.implementation,
                    implementationVtable,
                    handle.pointerInfo,
                    handle.pointerInfo->referenceCount,
                    handle.pointerInfo->deleteFunction,
                    handle.pointerInfo->data,
                    static_cast<int>(priority));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                std::snprintf(
                    detail,
                    std::size(detail),
                    "handle=%p handle_vtable=%p base_data=%p implementation=%p pointer_info=%p fields_readable=false priority=%d",
                    &handle,
                    handle.vtable,
                    handle.baseData,
                    handle.implementation,
                    handle.pointerInfo,
                    static_cast<int>(priority));
            }
            diagnostics_.Event("CreatureControlHandleAcquired", detail);
        }
        diagnostics_.Log(acquired
            ? "Creature control: native scripted control acquired."
            : "Creature control: native scripted control acquisition failed.");
        return acquired;
    }

    bool EntityService::ReleaseControlHandle(native::ScriptControlHandle& handle)
    {
        if (handle.implementation == nullptr && handle.pointerInfo == nullptr)
        {
            handle = {};
            return true;
        }

        bool released = false;
        char detail[512] = {};
        __try
        {
            const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
            native::CountedPointerInfo* const pointerInfo = handle.pointerInfo;
            const unsigned int referenceCount = pointerInfo != nullptr
                ? pointerInfo->referenceCount
                : 0;
            const bool valid =
                handle.vtable == nullptr &&
                handle.baseData == nullptr &&
                handle.implementation != nullptr &&
                pointerInfo != nullptr &&
                referenceCount > 0 &&
                referenceCount < 0x100000 &&
                pointerInfo->data == handle.implementation &&
                pointerInfo->deleteFunction == reinterpret_cast<void*>(
                    base + native::rva::ScriptedControlDeleteFunction) &&
                *reinterpret_cast<void**>(handle.implementation) ==
                    reinterpret_cast<void*>(
                        base + native::rva::ScriptedControlImplementationVtable);
            if (valid)
            {
                const unsigned int remaining = --pointerInfo->referenceCount;
                if (remaining == 0)
                {
                    reinterpret_cast<ControlDeleteFunction>(
                        pointerInfo->deleteFunction)(pointerInfo->data);
                    reinterpret_cast<GameHeapFreeFunction>(
                        base + native::rva::GameHeapFree)(pointerInfo);
                }
                std::snprintf(
                    detail,
                    std::size(detail),
                    "implementation=%p pointer_info=%p reference_count=%u->%u destroyed=%s",
                    handle.implementation,
                    pointerInfo,
                    referenceCount,
                    remaining,
                    remaining == 0 ? "true" : "false");
                handle = {};
                released = true;
            }
            else
            {
                std::snprintf(
                    detail,
                    std::size(detail),
                    "implementation=%p pointer_info=%p reference_count=%u valid=false",
                    handle.implementation,
                    pointerInfo,
                    referenceCount);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            std::snprintf(
                detail,
                std::size(detail),
                "implementation=%p pointer_info=%p structured_exception=true",
                handle.implementation,
                handle.pointerInfo);
        }
        diagnostics_.Event(
            released
                ? "CreatureControlHandleReleased"
                : "CreatureControlHandleReleaseFailed",
            detail);
        diagnostics_.Log(released
            ? "Creature control: native counted handle released."
            : "Creature control: refused or failed native counted-handle release.");
        return released;
    }

    void EntityService::ReleaseHandle(native::ScriptThing& handle) const
    {
        if (handle.vtable == nullptr)
        {
            handle = {};
            return;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        if (handle.vtable != reinterpret_cast<void**>(base + native::rva::ScriptThingVtable))
        {
            diagnostics_.Log("Entity API: refused to release a handle with an unexpected vtable.");
            return;
        }
        __try
        {
            const auto destroy = reinterpret_cast<ScriptThingDestructor>(
                base + native::rva::ScriptThingDestructor);
            destroy(&handle, 0);
            handle = {};
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            diagnostics_.Log("Entity API: CScriptThing release raised a structured exception.");
        }
    }

    bool EntityService::HasCapability(const std::string& name) const
    {
        if (!staticApiValidated_)
        {
            return false;
        }
        return name == "World.FindByScriptName" ||
            name == "World.CreateCreature" ||
            name == "Entity.State" ||
            name == "Entity.Teleport" ||
            name == "Entity.Flags" ||
            name == "Entity.Lifecycle" ||
            name == "Entity.Metadata.Read" ||
            name == "Entity.Metadata.Write" ||
            name == "Entity.Attack" ||
            name == "Creature.AcquireControl" ||
            name == "Creature.Navigation" ||
            name == "Creature.Animation" ||
            name == "Creature.CombatAnimation";
    }

    HMODULE EntityService::GameModule() const noexcept
    {
        return gameModule_;
    }

    native::GameInterfaceAccess& EntityService::Interface() noexcept
    {
        return interfaceAccess_;
    }

    const core::Diagnostics& EntityService::Diagnostics() const noexcept
    {
        return diagnostics_;
    }
}
