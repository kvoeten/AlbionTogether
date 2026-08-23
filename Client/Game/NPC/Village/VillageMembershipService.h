#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/NPC/Village/Hooks/VillageMembershipMutationHook.h"

#include <Windows.h>

namespace fable::game::npc::village
{
    class VillageMembershipService final
    {
    public:
        using MutationSink = VillageMembershipMutationHook::EventSink;

        bool Initialize(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        void SetMutationSink(MutationSink sink, void* context) noexcept;
        [[nodiscard]] bool Read(
            void* thing,
            native::VillageMembershipState& state) const noexcept;
        bool ApplyAuthoritative(
            void* thing,
            std::uint64_t villageUid,
            bool& changed) noexcept;

    private:
        VillageMembershipMutationHook hook_;
    };
}
