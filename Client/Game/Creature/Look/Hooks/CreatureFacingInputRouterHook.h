#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Math/Vector3.h"

#include <Windows.h>

#include <array>
#include <atomic>

namespace fable::game::creature::look
{
    class CreatureFacingInputRouterHook final
    {
    public:
        struct ReplicatedMovementInput final
        {
            Vector3 position = {};
            Vector3 velocity = {};
            float facing = 0.0f;
            float sampleAgeSeconds = 0.0f;
            bool moving = false;
        };

        using ReplicatedMovementProvider = bool(*)(
            void* context,
            void* creature,
            ReplicatedMovementInput& input);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        bool Bind(
            void* targetCreature,
            void* targetPhysicsNavigator,
            ReplicatedMovementProvider provider = nullptr,
            void* providerContext = nullptr);
        void Unbind(void* targetCreature) noexcept;
        void Clear() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] bool IsBound() const noexcept;
        [[nodiscard]] unsigned int RoutedFacingCount() const noexcept;

    private:
        struct Binding final
        {
            void* creature = nullptr;
            void* navigator = nullptr;
            ReplicatedMovementProvider provider = nullptr;
            void* providerContext = nullptr;
            ULONGLONG lastFrameAt = 0;
        };

        static constexpr std::size_t MaximumBindings = 8;

        static bool __fastcall ObserveCreatureUpdate(
            void* creature,
            void* unused);

        static CreatureFacingInputRouterHook* active_;

        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        ::fable::game::creature::native::CreatureFrameFunctions::UpdateFramePointer
            original_ = nullptr;
        void** vtableSlot_ = nullptr;
        mutable SRWLOCK bindingLock_ = SRWLOCK_INIT;
        std::array<Binding, MaximumBindings> bindings_ = {};
        std::atomic_uint routedFacingCount_{0};
    };
}
