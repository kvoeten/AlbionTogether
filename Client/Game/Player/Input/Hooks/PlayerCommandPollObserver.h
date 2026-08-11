#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Player/Input/Native/PlayerCommandPollFunction.h"

#include <Windows.h>

#include <atomic>

namespace fable::game::player::input
{
    class PlayerCommandPollObserver final
    {
    public:
        bool Install(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics);
        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static bool __fastcall Intercept(
            void* gamePlayerInterface,
            void*,
            void* outputCommand);

        static PlayerCommandPollObserver* active_;
        core::Diagnostics diagnostics_ = {};
        HMODULE gameModule_ = nullptr;
        void** vtableSlot_ = nullptr;
        native::PlayerCommandPollFunction::Pointer original_ = nullptr;
        std::atomic<unsigned int> observedCommandCount_{0};
    };
}
