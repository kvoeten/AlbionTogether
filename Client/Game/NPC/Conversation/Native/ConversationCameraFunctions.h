#pragma once

namespace fable::game::native
{
    class GameInterfaceAccess;
}

namespace fable::game::npc::conversation::native
{
    struct ConversationCameraFunctions final
    {
        static bool SetNoDialogCamera(
            fable::game::native::GameInterfaceAccess& interfaceAccess,
            bool disabled) noexcept;
    };
}
