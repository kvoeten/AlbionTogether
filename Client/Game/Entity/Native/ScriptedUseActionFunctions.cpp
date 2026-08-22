#include "ScriptedUseActionFunctions.h"

#include "ThingComponentAccess.h"

namespace fable::game::entity::native
{
    bool ScriptedUseActionFunctions::Execute(
        void* nativeThing,
        HMODULE gameModule,
        bool requireRegionEntrance,
        const char*& failure) noexcept
    {
        failure = "unknown-scripted-use-failure";
#if !defined(_M_IX86)
        (void)nativeThing;
        (void)gameModule;
        (void)requireRegionEntrance;
        failure = "scripted-use-requires-x86";
        return false;
#else
        if (nativeThing == nullptr || gameModule == nullptr)
        {
            failure = "native-thing-or-game-module-unavailable";
            return false;
        }

        void* component = ThingComponentAccess::Find(
            nativeThing,
            ThingComponentType::ScriptedUseAction);
        if (component == nullptr)
        {
            component = ThingComponentAccess::FindByVtableRva(
                nativeThing,
                gameModule,
                VtableRva);
        }
        if (component == nullptr)
        {
            failure = "ctc-action-use-scripted-hook-unavailable";
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        using ExecuteFunction = void(__thiscall*)(void*, void*);
        bool executed = false;
        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(component);
            void** const vtable = *reinterpret_cast<void***>(component);
            if (vtable != reinterpret_cast<void**>(base + VtableRva) ||
                vtable[ExecuteVtableSlot] !=
                    reinterpret_cast<void*>(base + ExecuteRva))
            {
                failure = "ctc-action-use-scripted-hook-vtable-mismatch";
                return false;
            }
            if (requireRegionEntrance &&
                bytes[TeleportToRegionEntranceOffset] == 0)
            {
                failure = "scripted-use-is-not-a-region-entrance";
                return false;
            }

            // The retail callback consumes this one-shot flag and performs
            // the action's configured animation, sound, and region-entry
            // behavior. No position write or collision crossing is involved.
            bytes[PendingUseOffset] = 1;
            const auto execute = reinterpret_cast<ExecuteFunction>(
                vtable[ExecuteVtableSlot]);
            execute(component, nullptr);
            executed = true;
            failure = nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            executed = false;
            failure = "scripted-use-callback-raised-structured-exception";
        }
        return executed;
#endif
    }
}
