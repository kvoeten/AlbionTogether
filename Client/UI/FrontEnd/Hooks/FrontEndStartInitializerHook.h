#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "UI/FrontEnd/Native/FrontEndStartInitializer.h"

#include <Windows.h>
#include <cstdint>

namespace fable::ui::front_end
{
    class FrontEndStartInitializerHook final
    {
    public:
        using InitializedCallback = void(*)(void* frontEndStart);

        bool Install(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics,
            InitializedCallback initializedCallback);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static void __fastcall Invoke(void* frontEndStart, void* unused);

        static FrontEndStartInitializerHook* active_;

        core::Diagnostics diagnostics_ = {};
        native::FrontEndStartInitializer::Pointer original_ = nullptr;
        std::uint8_t* target_ = nullptr;
        InitializedCallback initializedCallback_ = nullptr;
        void* trampoline_ = nullptr;
    };
}
