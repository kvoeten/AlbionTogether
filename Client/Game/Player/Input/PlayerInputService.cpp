#include "PlayerInputService.h"

namespace fable::game::player::input
{
    bool PlayerInputService::Initialize(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        return commandPollObserver_.Install(gameModule, diagnostics);
    }
}
