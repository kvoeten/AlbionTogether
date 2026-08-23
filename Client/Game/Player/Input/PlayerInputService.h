#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Player/Input/Hooks/PlayerCommandPollObserver.h"

#include <Windows.h>

namespace fable::game::player::input
{
    class PlayerInputService final
    {
    public:
        bool Initialize(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;

    private:
        PlayerCommandPollObserver commandPollObserver_;
    };
}
