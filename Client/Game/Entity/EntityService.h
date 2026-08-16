#pragma once

#include "../../Core/Diagnostics/Diagnostics.h"
#include "../Math/Vector3.h"
#include "../Native/GameInterface.h"
#include "../Native/ScriptTypes.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace fable::game
{
    class Entity;
    class ScriptControl;
    enum class AiPriority : int;

    class EntityService final
    {
    public:
        bool Initialize(HMODULE gameModule, const core::Diagnostics& diagnostics);

        Entity* GetHero();
        Entity* FindByScriptName(const std::string& scriptName);
        Entity* CreateCreature(
            const std::string& definition,
            const Vector3& position,
            const std::string& scriptName);

        [[nodiscard]] bool IsValid(const native::ScriptThing& handle) const;
        [[nodiscard]] bool IsAlive(const native::ScriptThing& handle) const;
        [[nodiscard]] bool IsDead(const native::ScriptThing& handle) const;
        [[nodiscard]] bool IsSneaking(const native::ScriptThing& handle) const;
        [[nodiscard]] bool IsAwareOfHero(const native::ScriptThing& handle) const;
        [[nodiscard]] bool IsUnconscious(const native::ScriptThing& handle) const;
        [[nodiscard]] bool IsUsable(const native::ScriptThing& handle) const;
        [[nodiscard]] bool IsOpenDoor(const native::ScriptThing& handle) const;
        [[nodiscard]] bool IsSummonedCreature(const native::ScriptThing& handle) const;
        [[nodiscard]] std::string GetName(const native::ScriptThing& handle) const;
        [[nodiscard]] std::string GetDefinitionName(const native::ScriptThing& handle) const;
        bool ResolveDefinitionName(
            std::uint16_t definitionIndex,
            std::string& definitionName) const;
        [[nodiscard]] std::string GetDataString(const native::ScriptThing& handle) const;
        [[nodiscard]] std::string GetCurrentMapName(const native::ScriptThing& handle) const;
        [[nodiscard]] std::string GetHomeMapName(const native::ScriptThing& handle) const;
        [[nodiscard]] std::uint64_t GetUid(const native::ScriptThing& handle) const;
        bool SetDataString(const native::ScriptThing& handle, const std::string& value);
        [[nodiscard]] bool GetActivationTriggerStatus(const native::ScriptThing& handle) const;
        [[nodiscard]] int GetScriptCounter(const native::ScriptThing& handle) const;
        bool SetUsable(const native::ScriptThing& handle, bool enabled);
        bool SetFriendsWithEverything(const native::ScriptThing& handle, bool enabled);
        bool SetActivationTriggerStatus(const native::ScriptThing& handle, bool enabled);
        bool SetKillOnLevelUnload(const native::ScriptThing& handle, bool enabled);
        bool RequestDestroy(
            const native::ScriptThing& handle,
            bool immediate = false);
        bool RequestDestroyNative(
            void* nativeThing,
            bool immediate = false);
        bool UpdateAttachment(const native::ScriptThing& handle);
        bool IncrementScriptCounter(const native::ScriptThing& handle);
        bool DecrementScriptCounter(const native::ScriptThing& handle);
        bool ReadPosition(const native::ScriptThing& handle, Vector3& position) const;
        bool ReadFacing(const native::ScriptThing& handle, float& facing) const;
        [[nodiscard]] void* ResolveNative(const native::ScriptThing& handle) const;
        bool Teleport(
            const native::ScriptThing& handle,
            const Vector3& position,
            float facing,
            bool effect);
        bool SetAttackable(const native::ScriptThing& handle, bool enabled);
        bool SetDamageable(const native::ScriptThing& handle, bool enabled);
        bool SetCollidable(const native::ScriptThing& handle, bool enabled);
        bool SetDrawable(const native::ScriptThing& handle, bool enabled);
        bool Attack(
            const native::ScriptThing& attacker,
            const native::ScriptThing& target,
            bool stopCurrentAction,
            bool unsheathe);

        ScriptControl* AcquireControl(
            const native::ScriptThing& entity,
            AiPriority priority);
        bool StartControl(
            const native::ScriptThing& entity,
            AiPriority priority,
            native::ScriptControlHandle& handle);
        bool ReleaseControlHandle(native::ScriptControlHandle& handle);
        void ReleaseHandle(native::ScriptThing& handle) const;

        [[nodiscard]] bool HasCapability(const std::string& name) const;
        [[nodiscard]] HMODULE GameModule() const noexcept;
        [[nodiscard]] native::GameInterfaceAccess& Interface() noexcept;
        [[nodiscard]] const core::Diagnostics& Diagnostics() const noexcept;

    private:
        native::GameScriptInterface* ResolveInterface() const;
        Entity* FindByScriptNameNative(const char* scriptName);
        bool FindByScriptNameHandleNative(
            const char* scriptName,
            native::ScriptThing& result);
        Entity* CreateCreatureNative(
            const char* definition,
            const Vector3& position,
            const char* scriptName);
        bool CreateCreatureHandleNative(
            const char* definition,
            const Vector3& position,
            const char* scriptName,
            native::ScriptThing& result);
        bool SetFlag(
            const native::ScriptThing& handle,
            std::size_t vtableIndex,
            bool enabled) const;
        bool CallScriptThingPredicate(
            const native::ScriptThing& handle,
            std::size_t vtableIndex,
            bool fallback) const;
        bool CallScriptThingSetter(
            const native::ScriptThing& handle,
            std::size_t vtableIndex,
            bool value) const;
        bool CallScriptThingVoid(
            const native::ScriptThing& handle,
            std::size_t vtableIndex) const;
        bool ReadBorrowedString(
            const native::ScriptThing& handle,
            std::size_t vtableIndex,
            std::string& value) const;
        bool ReadOwnedString(
            const native::ScriptThing& handle,
            std::size_t vtableIndex,
            std::string& value) const;

        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        bool staticApiValidated_ = false;
        bool metadataApiValidated_ = false;
        bool interactionApiValidated_ = false;
        bool lifecycleApiValidated_ = false;
        bool definitionLookupApiValidated_ = false;
        native::GameInterfaceAccess interfaceAccess_;
    };
}
