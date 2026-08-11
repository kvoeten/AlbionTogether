#include "CreatureCombatFunctions.h"

#include "Game/Native/Addresses.h"

namespace fable::game::creature::combat::native
{
    bool CreatureCombatFunctions::ValidateDefinitions(
        ::fable::game::native::GameInterfaceAccess& interfaceAccess) noexcept
    {
        const HMODULE gameModule = interfaceAccess.GameModule();
        if (gameModule == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        bool valid = false;
        __try
        {
            auto** const vtable = reinterpret_cast<void**>(
                base + ::fable::game::native::rva::GameScriptInterfaceVtable);
            valid = vtable[GetHeroTargetedThingSlot] ==
                reinterpret_cast<void*>(base + GetHeroTargetedThingRva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        return valid;
    }

    bool CreatureCombatFunctions::GetHeroTargetedThing(
        ::fable::game::native::GameInterfaceAccess& interfaceAccess,
        ::fable::game::native::ScriptThing& result) noexcept
    {
        result = {};
        auto* const gameInterface = interfaceAccess.Resolve();
        const auto function = reinterpret_cast<GetHeroTargetedThingPointer>(
            interfaceAccess.ResolveFunction(
                GetHeroTargetedThingSlot,
                GetHeroTargetedThingRva));
        if (gameInterface == nullptr || function == nullptr)
        {
            return false;
        }

        bool returnedResult = false;
        __try
        {
            returnedResult = function(gameInterface, &result) == &result;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            returnedResult = false;
            result = {};
        }
        return returnedResult;
    }

}
