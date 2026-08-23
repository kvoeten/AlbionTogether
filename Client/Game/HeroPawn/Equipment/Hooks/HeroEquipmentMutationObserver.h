#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace fable::game::hero_pawn::equipment::hooks
{
    class HeroEquipmentMutationObserver final
    {
    public:
        using EventSink = void(*)(void* context, void* component);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        void SetEventSink(EventSink sink, void* context) noexcept;
        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        using ReconcilePresentation = void(__thiscall*)(void*);
        static constexpr std::size_t DisplacedBytes = 5;

        static void __fastcall Observe(void* component, void* unused);
        bool InstallDetour(
            std::uint8_t* target,
            void* replacement) noexcept;

        static HeroEquipmentMutationObserver* active_;
        core::Diagnostics diagnostics_ = {};
        ReconcilePresentation original_ = nullptr;
        std::uint8_t* target_ = nullptr;
        void* trampoline_ = nullptr;
        std::array<std::uint8_t, DisplacedBytes> originalBytes_ = {};
        std::atomic<EventSink> eventSink_{nullptr};
        std::atomic<void*> eventSinkContext_{nullptr};
    };
}
