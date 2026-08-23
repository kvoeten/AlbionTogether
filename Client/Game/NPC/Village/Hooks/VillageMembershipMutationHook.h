#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/NPC/Village/Native/VillageMembershipFunctions.h"
#include "Game/NPC/Village/VillageMembershipMutationEvent.h"

#include <Windows.h>

#include <atomic>
#include <array>

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
        void* trampoline_ = nullptr;
        std::uint8_t* target_ = nullptr;
        std::array<std::uint8_t,
            native::VillageMembershipFunctions::SetVillagePointerDisplacedBytes>
                originalBytes_ = {};
        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::atomic<EventSink> eventSink_{nullptr};
        std::atomic<void*> eventSinkContext_{nullptr};
        std::atomic_uint observedCount_{0};
    };
}
