#include "DummyVillagerService.h"

namespace fable::game::npc::simulation
{
    bool DummyVillagerService::Initialize(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        if (!hook_.Install(gameModule, diagnostics))
        {
            Shutdown();
            return false;
        }
        return true;
    }

    void DummyVillagerService::Shutdown() noexcept
    {
        hook_.Shutdown();
    }

    void DummyVillagerService::SetMutationSink(
        MutationSink sink,
        void* context) noexcept
    {
        hook_.SetEventSink(sink, context);
    }

    void DummyVillagerService::SetProjectionSink(
        ProjectionSink sink,
        void* context) noexcept
    {
        hook_.SetProjectionSink(sink, context);
    }

    bool DummyVillagerService::Read(
        void* thing,
        DummyVillagerState& state) const noexcept
    {
        return hook_.Read(thing, state);
    }

    bool DummyVillagerService::ApplyAuthoritative(
        void* thing,
        const DummyVillagerState& state,
        bool& changed) noexcept
    {
        return hook_.ApplyAuthoritative(thing, state, changed);
    }
}
