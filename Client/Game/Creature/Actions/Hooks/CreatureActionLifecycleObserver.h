#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Actions/CreatureActionLifecycleEvent.h"
#include "Game/Creature/Actions/Native/CreatureActionFunctions.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <unordered_set>

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
        using PostUpdateSink = void(*)(
            void* context,
            CreatureActionLifecycleObserver& observer,
            void* creature);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        bool AddEventSink(EventSink sink, void* context) noexcept;
        void RemoveEventSink(EventSink sink, void* context) noexcept;
        bool AddPostUpdateSink(PostUpdateSink sink, void* context) noexcept;
        void RemovePostUpdateSink(
            PostUpdateSink sink,
            void* context) noexcept;
        // Legacy single-consumer facade. New independent consumers should use
        // AddEventSink/RemoveEventSink so action observation stays composable.
        void SetEventSink(EventSink sink, void* context) noexcept;
        void SetAuthorityGate(AuthorityGate gate, void* context) noexcept;
        static void BeginAuthoritativeReplay() noexcept;
        static void EndAuthoritativeReplay() noexcept;
        // The retail ability wrapper returns void. A scoped receipt lets its
        // caller distinguish "function returned" from "Fable accepted an
        // action" without polling or publishing rejected input requests.
        static bool BeginSubmissionReceipt(void* creature) noexcept;
        static bool EndSubmissionReceipt(
            void* creature,
            bool& accepted) noexcept;
        // Ends a frozen locally-originated action before an authoritative
        // replica replaces it. An action already installed by replication is
        // never interrupted here; Fable retains its normal priority rules.
        static bool RetireLocalActionForAuthoritativeReplay(
            void* creature) noexcept;
        // Re-enters the observed native submission boundary on the caller's
        // current thread. Deferred native actions use this after the owning
        // creature's action update has retired the action that blocked them.
        bool RetrySubmission(
            void* creature,
            void* action,
            bool authoritativeReplay) noexcept;
        [[nodiscard]] bool IsAuthoritativeReplayAction(
            void* action) const noexcept;
        static bool DescribeActionType(
            void* action,
            char* name,
            std::size_t capacity) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] unsigned int SubmissionCount() const noexcept;
        [[nodiscard]] unsigned int FinishCount() const noexcept;

    private:
        static constexpr unsigned int DiagnosticEventLimit = 2048;
        static constexpr std::size_t ActionNameCapacity = 128;
        static constexpr std::size_t EventSinkCapacity = 4;
        static constexpr std::size_t PostUpdateSinkCapacity = 4;

        struct EventSubscription final
        {
            EventSink sink = nullptr;
            void* context = nullptr;
        };

        struct PostUpdateSubscription final
        {
            PostUpdateSink sink = nullptr;
            void* context = nullptr;
        };

        struct Detour final
        {
            std::uint8_t* target = nullptr;
            void* trampoline = nullptr;
            std::array<std::uint8_t, 8> originalBytes = {};
            std::size_t displacedBytes = 0;
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
            std::size_t displacedBytes,
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
        static void* ReadAttackTarget(
            HMODULE gameModule,
            void* action,
            const char* actionType) noexcept;
        static bool ActionMayCarryAnimation(const char* actionType) noexcept;
        static void* ResolveActionOwner(void* action) noexcept;
        static ThingContext ReadThingContext(void* creature) noexcept;
        void Notify(const CreatureActionLifecycleEvent& event) noexcept;
        void NotifyPostUpdate(void* creature) noexcept;
        bool IsAuthoritativeReplay(void* action) const noexcept;
        void RememberAuthoritativeReplay(void* action) noexcept;
        void ForgetAuthoritativeReplay(void* action) noexcept;

        static CreatureActionLifecycleObserver* active_;

        core::Diagnostics diagnostics_ = {};
        HMODULE gameModule_ = nullptr;
        native::CreatureActionFunctions::UpdatePointer originalUpdate_ = nullptr;
        native::CreatureActionFunctions::SubmitPointer originalSubmit_ = nullptr;
        native::CreatureActionFunctions::FinishPointer originalFinish_ = nullptr;
        Detour updateDetour_ = {};
        Detour submitDetour_ = {};
        Detour finishDetour_ = {};
        std::atomic_uint submissionCount_{0};
        std::atomic_uint finishCount_{0};
        mutable SRWLOCK eventSinkLock_ = SRWLOCK_INIT;
        std::array<EventSubscription, EventSinkCapacity> eventSinks_ = {};
        mutable SRWLOCK postUpdateSinkLock_ = SRWLOCK_INIT;
        std::array<PostUpdateSubscription, PostUpdateSinkCapacity>
            postUpdateSinks_ = {};
        std::atomic<AuthorityGate> authorityGate_{nullptr};
        std::atomic<void*> authorityGateContext_{nullptr};
        mutable SRWLOCK authoritativeReplayLock_ = SRWLOCK_INIT;
        std::unordered_set<void*> authoritativeReplayActions_;
        static thread_local unsigned int authoritativeReplayDepth_;
        static thread_local void* submissionReceiptCreature_;
        static thread_local unsigned int submissionReceiptDepth_;
        static thread_local bool submissionReceiptObserved_;
        static thread_local bool submissionReceiptAccepted_;
    };
}
