#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Math/Vector3.h"

#include <Windows.h>

#include <atomic>
#include <memory>
#include <unordered_map>

namespace fable::game::creature::look
{
    class CreatureFacingInputRouterHook final
    {
    public:
        struct ReplicatedMovementInput final
        {
            std::uint64_t actorId = 0;
            Vector3 position = {};
            Vector3 velocity = {};
            float facing = 0.0f;
            float angularVelocity = 0.0f;
            float sampleAgeSeconds = 0.0f;
            bool moving = false;
        };

        using ReplicatedMovementProvider = bool(*)(
            void* context,
            void* creature,
            ReplicatedMovementInput& input);
        using FrameObserver = void(*)(void* context, void* creature);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        bool Bind(
            void* targetCreature,
            void* targetPhysicsNavigator,
            ReplicatedMovementProvider provider = nullptr,
            void* providerContext = nullptr);
        void Unbind(void* targetCreature) noexcept;
        bool Drive(void* targetCreature);
        void SetFrameObserver(
            FrameObserver observer,
            void* context) noexcept;
        void Clear() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] bool IsBound() const noexcept;
        [[nodiscard]] unsigned int RoutedFacingCount() const noexcept;

    private:
        struct Binding final
        {
            std::atomic<void*> navigator{nullptr};
            std::atomic<ReplicatedMovementProvider> provider{nullptr};
            std::atomic<void*> providerContext{nullptr};
            std::atomic<ULONGLONG> lastFrameAt{0};
            std::atomic<ULONGLONG> lastNativeMovementReportAt{0};
            std::atomic<ULONGLONG> lastBackgroundMovementReportAt{0};
            std::atomic_bool nativeMoving{false};
            std::atomic_bool backgroundMoving{false};
        };

        struct BindingSnapshot final
        {
            void* navigator = nullptr;
            ReplicatedMovementProvider provider = nullptr;
            void* providerContext = nullptr;
            ULONGLONG lastFrameAt = 0;
        };

        [[nodiscard]] std::shared_ptr<Binding> FindBinding(
            void* creature) const noexcept;
        [[nodiscard]] bool SnapshotBinding(
            void* creature,
            BindingSnapshot& snapshot,
            ULONGLONG frameAt = 0) noexcept;
        [[nodiscard]] bool UpdateMovementReport(
            void* creature,
            bool nativeFrame,
            bool moving,
            ULONGLONG now) noexcept;

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
        std::unordered_map<void*, std::shared_ptr<Binding>> bindings_;
        std::atomic<FrameObserver> frameObserver_{nullptr};
        std::atomic<void*> frameObserverContext_{nullptr};
        std::atomic_uint routedFacingCount_{0};
        std::atomic_uint backgroundMovementCount_{0};
    };
}
