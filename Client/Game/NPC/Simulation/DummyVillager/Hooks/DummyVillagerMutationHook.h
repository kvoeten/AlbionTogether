#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/NPC/Simulation/DummyVillager/DummyVillagerMutationEvent.h"
#include "Game/NPC/Simulation/DummyVillager/Native/DummyVillagerFunctions.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fable::game::npc::simulation
{
    class DummyVillagerMutationHook final
    {
    public:
        using EventSink = void(*)(
            void* context,
            const DummyVillagerMutationEvent& event);
        using ProjectionSink = bool(*)(
            void* context,
            std::uint64_t thingUid,
            DummyVillagerState& state);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void SetEventSink(EventSink sink, void* context) noexcept;
        void SetProjectionSink(ProjectionSink sink, void* context) noexcept;
        bool Read(void* thing, DummyVillagerState& state) const noexcept;
        bool ApplyAuthoritative(
            void* thing,
            const DummyVillagerState& state,
            bool& changed) noexcept;
        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        struct Detour final
        {
            std::uint8_t* target = nullptr;
            void* trampoline = nullptr;
            std::size_t displacedBytes = 0;
            std::array<std::uint8_t, 8> originalBytes = {};
        };

        static void __fastcall MaterializeIntercept(
            void* component,
            void* unused);
        static void __fastcall ScheduleIntercept(
            void* component,
            void* unused);
        static bool __fastcall SerializeIntercept(
            void* component,
            void* unused,
            void* context,
            void* serializer);
        static void ObserveAfter(
            DummyVillagerMutationHook& hook,
            void* component,
            const DummyVillagerState& previous);
        static void* ReadOwnerThing(void* component) noexcept;
        static std::uint64_t ReadThingUid(void* thing) noexcept;
        bool InstallDetour(
            std::uint8_t* target,
            void* replacement,
            std::size_t displacedBytes,
            Detour& detour,
            native::DummyVillagerFunctions::UpdatePointer& original) noexcept;
        bool InstallSerializeDetour(
            std::uint8_t* target,
            void* replacement,
            std::size_t displacedBytes,
            Detour& detour,
            native::DummyVillagerFunctions::SerializePointer& original)
            noexcept;
        static void RestoreDetour(Detour& detour) noexcept;

        static DummyVillagerMutationHook* active_;
        static thread_local unsigned int authoritativeApplyDepth_;

        HMODULE gameModule_ = nullptr;
        native::DummyVillagerFunctions::UpdatePointer originalMaterialize_ =
            nullptr;
        native::DummyVillagerFunctions::UpdatePointer originalSchedule_ =
            nullptr;
        native::DummyVillagerFunctions::SerializePointer originalSerialize_ =
            nullptr;
        Detour materializeDetour_ = {};
        Detour scheduleDetour_ = {};
        Detour serializeDetour_ = {};
        core::Diagnostics diagnostics_ = {};
        std::atomic<EventSink> eventSink_{nullptr};
        std::atomic<void*> eventSinkContext_{nullptr};
        std::atomic<ProjectionSink> projectionSink_{nullptr};
        std::atomic<void*> projectionSinkContext_{nullptr};
        std::atomic_uint observedCount_{0};
    };
}
