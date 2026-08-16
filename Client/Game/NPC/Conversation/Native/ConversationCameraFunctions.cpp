#include "ConversationCameraFunctions.h"

#include "Game/Native/Addresses.h"
#include "Game/Native/GameInterface.h"

#include <Windows.h>

namespace fable::game::npc::conversation::native
{
    bool ConversationCameraFunctions::SetNoDialogCamera(
        fable::game::native::GameInterfaceAccess& interfaceAccess,
        bool disabled) noexcept
    {
        using Function = void(__thiscall*)(
            fable::game::native::GameScriptInterface*,
            bool);
        fable::game::native::GameScriptInterface* const gameInterface =
            interfaceAccess.Resolve();
        const auto function = reinterpret_cast<Function>(
            interfaceAccess.ResolveFunction(
                fable::game::native::game_interface_slot::SetNoDialogCamera,
                fable::game::native::rva::SetNoDialogCamera));
        if (gameInterface == nullptr || function == nullptr)
        {
            return false;
        }
        __try
        {
            function(gameInterface, disabled);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}
