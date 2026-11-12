#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/Native/GameInterface.h"
#include "Game/Native/ScriptTypes.h"
#include "Game/Quest/Runtime/QuestLifecycleEvent.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace fable::game::quest
{
    // Observes the four CQuestManager lifecycle mutations after retail has
    // applied them. This class neither polls quest fields nor owns networking.
    class QuestLifecycleHook final
    {
    public:
        using EventSink = void(*)(
            void* context,
            const QuestLifecycleEvent& event) noexcept;

        bool Install(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics) noexcept;
        void Shutdown() noexcept;
        void SetEventSink(EventSink sink, void* context) noexcept;

        // Applies a host-approved transition through the original native
        // entry point. Nested quest lifecycle calls are allowed to run but are
        // marked as authoritative replay and are never observed again.
        bool ApplyAuthoritative(
            QuestLifecycleOperation operation,
            const char* name,
            const std::uint32_t (&arguments)[3],
            bool& changed) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] bool IsManagerReady() const noexcept;
        [[nodiscard]] bool ReadState(
            const char* name,
            QuestLifecycleState& state) const noexcept;

    private:
        using ActivateFunction = bool(__thiscall*)(
            void*, native::CharString*, std::uint32_t, std::uint32_t);
        using DeactivateFunction = void(__thiscall*)(
            void*, native::CharString*, std::uint32_t);
        using ResolveFunction = void(__thiscall*)(
            void*, native::CharString*, std::uint32_t, std::uint32_t,
            std::uint32_t);
        using PredicateFunction = bool(__thiscall*)(
            native::GameScriptInterface*, const native::CharString*);

        static bool __fastcall ActivateObserved(
            void* manager,
            void*,
            native::CharString* name,
            std::uint32_t argument0,
            std::uint32_t argument1);
        static void __fastcall DeactivateObserved(
            void* manager,
            void*,
            native::CharString* name,
            std::uint32_t argument0);
        static void __fastcall CompleteObserved(
            void* manager,
            void*,
            native::CharString* name,
            std::uint32_t argument0,
            std::uint32_t argument1,
            std::uint32_t argument2);
        static void __fastcall FailObserved(
            void* manager,
            void*,
            native::CharString* name,
            std::uint32_t argument0,
            std::uint32_t argument1,
            std::uint32_t argument2);

        [[nodiscard]] bool ReadState(
            native::GameScriptInterface* gameInterface,
            const native::CharString& name,
            QuestLifecycleState& state) const noexcept;
        [[nodiscard]] void* Manager() const noexcept;
        void Publish(
            QuestLifecycleOperation operation,
            const char* name,
            const std::uint32_t (&arguments)[3],
            const QuestLifecycleState& before,
            const QuestLifecycleState& after) noexcept;
        [[nodiscard]] bool CallOriginal(
            QuestLifecycleOperation operation,
            void* manager,
            native::CharString* name,
            const std::uint32_t (&arguments)[3]) noexcept;

        static QuestLifecycleHook* active_;
        static thread_local std::uint32_t authoritativeApplyDepth_;

        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        native::GameInterfaceAccess gameInterface_;
        core::hooking::InlineHook activatePatch_;
        core::hooking::InlineHook deactivatePatch_;
        core::hooking::InlineHook completePatch_;
        core::hooking::InlineHook failPatch_;
        ActivateFunction originalActivate_ = nullptr;
        DeactivateFunction originalDeactivate_ = nullptr;
        ResolveFunction originalComplete_ = nullptr;
        ResolveFunction originalFail_ = nullptr;
        std::atomic<EventSink> eventSink_{nullptr};
        std::atomic<void*> eventSinkContext_{nullptr};
    };
}
