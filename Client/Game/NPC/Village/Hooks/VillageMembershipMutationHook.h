#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/NPC/Village/Native/VillageMembershipFunctions.h"
#include "Game/NPC/Village/VillageMembershipMutationEvent.h"

#include <Windows.h>

#include <atomic>

namespace fable::game::npc::village
{
    class VillageMembershipMutationHook final
    {
    public:
        using EventSink = void(*)(
            void* context,
            const VillageMembershipMutationEvent& event);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        void SetEventSink(EventSink sink, void* context) noexcept;
        [[nodiscard]] bool Read(
            void* thing,
            native::VillageMembershipState& state) const noexcept;
        bool ApplyAuthoritative(
            void* thing,
            std::uint64_t villageUid,
            bool& changed) noexcept;
        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static void __fastcall Intercept(
            void* villageMember,
            void* unused,
            void* villageComponent);
        static void* ReadOwnerThing(void* villageMember) noexcept;
        static std::uint64_t ReadThingUid(void* thing) noexcept;
        static std::uint64_t ReadLinkedVillageUid(
            void* villageMember) noexcept;

        static VillageMembershipMutationHook* active_;
        static thread_local unsigned int authoritativeApplyDepth_;

        native::VillageMembershipFunctions::SetVillagePointer original_ =
            nullptr;
        core::hooking::InlineHook patch_;
        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::atomic<EventSink> eventSink_{nullptr};
        std::atomic<void*> eventSinkContext_{nullptr};
        std::atomic_uint observedCount_{0};
    };
}
