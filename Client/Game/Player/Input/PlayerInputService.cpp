#include "PlayerInputService.h"

namespace fable::game::player::input
{
    bool PlayerInputService::Initialize(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        if (!commandPollObserver_.Install(gameModule, diagnostics))
        {
            Shutdown();
            return false;
        }
        return true;
    }

    void PlayerInputService::Shutdown() noexcept
    {
        commandPollObserver_.Shutdown();
    }
}
