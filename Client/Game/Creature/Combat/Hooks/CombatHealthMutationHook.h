#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Combat/CombatHealthMutationEvent.h"
#include "Game/Creature/Combat/Native/CombatHealthMutationFunction.h"

#include <Windows.h>

#include <atomic>

namespace fable::game::creature::combat
{
    class CombatHealthMutationHook final
    {
    public:
        using EventSink = void(*)(
            void* context,
            const CombatHealthMutationEvent& event);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void SetEventSink(EventSink sink, void* context) noexcept;
        bool ApplyAuthoritative(
            void* creature,
            float currentHealth,
            float maximumHealth) noexcept;
        bool Read(
            void* creature,
            float& currentHealth,
            float& maximumHealth) const noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static void __fastcall Intercept(
            void* creature,
            void* unused,
            float delta,
            bool combatFlag);
        static std::uint64_t ReadThingUid(void* creature) noexcept;

        static CombatHealthMutationHook* active_;
        native::CombatHealthMutationFunction::Pointer original_ = nullptr;
        void* trampoline_ = nullptr;
        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::atomic<EventSink> eventSink_{nullptr};
        std::atomic<void*> eventSinkContext_{nullptr};
        std::atomic_uint observedCount_{0};
    };
}
