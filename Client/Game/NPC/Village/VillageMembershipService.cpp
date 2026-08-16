#include "VillageMembershipService.h"

namespace fable::game::npc::village
{
    bool VillageMembershipService::Initialize(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        return hook_.Install(gameModule, diagnostics);
    }

    void VillageMembershipService::SetMutationSink(
        MutationSink sink,
        void* context) noexcept
    {
        hook_.SetEventSink(sink, context);
    }

    bool VillageMembershipService::Read(
        void* thing,
        native::VillageMembershipState& state) const noexcept
    {
        return hook_.Read(thing, state);
    }

    bool VillageMembershipService::ApplyAuthoritative(
        void* thing,
        std::uint64_t villageUid,
        bool& changed) noexcept
    {
        return hook_.ApplyAuthoritative(thing, villageUid, changed);
    }
}
