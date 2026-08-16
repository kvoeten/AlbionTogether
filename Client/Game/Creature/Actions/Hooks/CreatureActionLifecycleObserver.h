#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Actions/CreatureActionLifecycleEvent.h"
#include "Game/Creature/Actions/Native/CreatureActionFunctions.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::actions
{
    class CreatureActionLifecycleObserver final
    {
    public:
        using EventSink = void(*)(
            void* context,
            const CreatureActionLifecycleEvent& event);
        using AuthorityGate = bool(*)(
            void* context,
            void* creature,
            void* action);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void SetEventSink(EventSink sink, void* context) noexcept;
        void SetAuthorityGate(AuthorityGate gate, void* context) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] unsigned int SubmissionCount() const noexcept;
        [[nodiscard]] unsigned int FinishCount() const noexcept;

    private:
        static constexpr unsigned int DiagnosticEventLimit = 2048;
        static constexpr std::size_t ActionNameCapacity = 128;

        struct Detour final
        {
            std::uint8_t* target = nullptr;
            void* trampoline = nullptr;
            std::array<std::uint8_t, native::CreatureActionFunctions::DisplacedBytes>
                originalBytes = {};
        };

        struct ThingContext final
        {
            std::uint64_t uid = 0;
            std::uint16_t mapId = 0;
            bool readable = false;
        };

        static void __fastcall ObserveUpdate(
            void* creature,
            void* unused);
        static bool __fastcall ObserveSubmission(
            void* creature,
            void* unused,
            void* action);
        static void __fastcall ObserveFinish(void* action, void* unused);

        bool InstallDetour(
            std::uint8_t* target,
            void* replacement,
            Detour& detour) noexcept;
        void RestoreDetour(Detour& detour) noexcept;
        void ReportSubmission(
            void* creature,
            void* requestedAction,
            const char* requestedType,
            bool accepted,
            bool authorityDenied) noexcept;
        void ReportFinish(
            void* action,
            void* creature,
            const char* actionType) noexcept;

        static bool ReadActionType(
            void* action,
            char (&name)[ActionNameCapacity]) noexcept;
        static std::uint32_t ReadAnimationId(void* action) noexcept;
        static bool ActionMayCarryAnimation(const char* actionType) noexcept;
        static void* ResolveActionOwner(void* action) noexcept;
        static ThingContext ReadThingContext(void* creature) noexcept;
        void Notify(const CreatureActionLifecycleEvent& event) noexcept;

        static CreatureActionLifecycleObserver* active_;

        core::Diagnostics diagnostics_ = {};
        native::CreatureActionFunctions::UpdatePointer originalUpdate_ = nullptr;
        native::CreatureActionFunctions::SubmitPointer originalSubmit_ = nullptr;
        native::CreatureActionFunctions::FinishPointer originalFinish_ = nullptr;
        Detour updateDetour_ = {};
        Detour submitDetour_ = {};
        Detour finishDetour_ = {};
        std::atomic_uint submissionCount_{0};
        std::atomic_uint finishCount_{0};
        std::atomic<EventSink> eventSink_{nullptr};
        std::atomic<void*> eventSinkContext_{nullptr};
        std::atomic<AuthorityGate> authorityGate_{nullptr};
        std::atomic<void*> authorityGateContext_{nullptr};
    };
}
