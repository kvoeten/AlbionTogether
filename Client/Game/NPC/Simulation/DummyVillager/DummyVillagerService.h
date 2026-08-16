#pragma once

#include "Game/NPC/Simulation/DummyVillager/Hooks/DummyVillagerMutationHook.h"

#include <Windows.h>

namespace fable::game::npc::simulation
{
    class DummyVillagerService final
    {
    public:
        using MutationSink = DummyVillagerMutationHook::EventSink;
        using ProjectionSink = DummyVillagerMutationHook::ProjectionSink;

        bool Initialize(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void SetMutationSink(MutationSink sink, void* context) noexcept;
        void SetProjectionSink(ProjectionSink sink, void* context) noexcept;
        bool Read(void* thing, DummyVillagerState& state) const noexcept;
        bool ApplyAuthoritative(
            void* thing,
            const DummyVillagerState& state,
            bool& changed) noexcept;

    private:
        DummyVillagerMutationHook hook_;
    };
}
