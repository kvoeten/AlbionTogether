#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Locomotion/Native/FollowCreatureActionFunctions.h"

#include <Windows.h>

#include <atomic>

namespace fable::game::creature::locomotion
{
    class FollowCreatureActionHook final
    {
    public:
        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] unsigned int StartCount() const noexcept;
        [[nodiscard]] unsigned int TickCount() const noexcept;

    private:
        static void __fastcall ObserveStart(void* action, void* unused);
        static void __fastcall ObserveTick(void* action, void* unused);

        void Report(
            const char* state,
            const char* boundary,
            void* action,
            unsigned int ordinal) const;

        static FollowCreatureActionHook* active_;

        core::Diagnostics diagnostics_ = {};
        native::FollowCreatureActionFunctions::ActionMethodPointer originalStart_ = nullptr;
        native::FollowCreatureActionFunctions::ActionMethodPointer originalTick_ = nullptr;
        void* startTrampoline_ = nullptr;
        void* tickTrampoline_ = nullptr;
        std::atomic_uint startCount_{0};
        std::atomic_uint tickCount_{0};
    };
}
