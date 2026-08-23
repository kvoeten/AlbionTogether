#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "UI/FrontEnd/Native/FrontEndLifecycleFunctions.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::ui::front_end
{
    struct FrontEndLifecycleCallbacks final
    {
        using Callback = void(__stdcall*)(void* object, const void* frame);

        Callback uiPageDoBegin = nullptr;
        Callback uiPageDoInit = nullptr;
        Callback uiPageStartPlay = nullptr;
        Callback playLoadMapMovie = nullptr;
        Callback frontEndStartDoInit = nullptr;
        Callback frontEndStartDoTick = nullptr;

        [[nodiscard]] bool IsComplete() const noexcept;
    };

    struct FrontEndLifecycleThunkAccess;

    class FrontEndLifecycleHooks final
    {
    public:
        bool Install(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics,
            const FrontEndLifecycleCallbacks& callbacks);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        using Boundary = native::FrontEndLifecycleFunctions::Boundary;
        static constexpr std::size_t BoundaryCount =
            static_cast<std::size_t>(Boundary::Count);

        void Dispatch(Boundary boundary, void* object, const void* frame) const;

        friend struct FrontEndLifecycleThunkAccess;
        static FrontEndLifecycleHooks* active_;

        core::Diagnostics diagnostics_ = {};
        std::array<FrontEndLifecycleCallbacks::Callback, BoundaryCount> callbacks_ = {};
        std::array<std::uint8_t*, BoundaryCount> targets_ = {};
        std::array<
            std::array<std::uint8_t, native::FrontEndLifecycleFunctions::DisplacedBytes>,
            BoundaryCount> originalBytes_ = {};
        bool installed_ = false;
    };
}
