#include "CreatureFrameInputRouterHook.h"

#include "Game/Creature/Locomotion/Native/PhysicsNavigatorFunctions.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace fable::game::creature::locomotion
{
    CreatureFrameInputRouterHook* CreatureFrameInputRouterHook::active_ = nullptr;

    bool CreatureFrameInputRouterHook::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;
        gameModule_ = gameModule;

#if !defined(_M_IX86)
        diagnostics_.Log(
            "Hook: player frame-input routing is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another player frame-input router is already active.");
            return false;
        }

        void** slot = nullptr;
        ::fable::game::creature::native::CreatureFrameFunctions::UpdateFramePointer
            original = nullptr;
        if (!::fable::game::creature::native::CreatureFrameFunctions::ResolvePlayerUpdateFrameSlot(
                gameModule,
                &slot,
                original))
        {
            diagnostics_.Log(
                "Hook: player/creature frame-update definitions failed validation.");
            return false;
        }

        DWORD previousProtection = 0;
        if (!VirtualProtect(
                slot,
                sizeof(*slot),
                PAGE_READWRITE,
                &previousProtection))
        {
            diagnostics_.Log(
                "Hook: player frame-update vtable protection change failed.");
            return false;
        }

        original_ = original;
        vtableSlot_ = slot;
        active_ = this;
        *slot = reinterpret_cast<void*>(
            &CreatureFrameInputRouterHook::ObservePlayerUpdate);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));

        DWORD discarded = 0;
        if (!VirtualProtect(
                slot,
                sizeof(*slot),
                previousProtection,
                &discarded))
        {
            diagnostics_.Log(
                "Hook: player frame-input router installed, but vtable protection restoration failed.");
        }

        char detail[256] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "slot=%p original=%p player_update_rva=0x%08X creature_update_rva=0x%08X",
            slot,
            reinterpret_cast<void*>(original_),
            static_cast<unsigned int>(
                ::fable::game::creature::native::CreatureFrameFunctions::PlayerCreatureUpdateFrameRva),
            static_cast<unsigned int>(
                ::fable::game::creature::native::CreatureFrameFunctions::CreatureUpdateFrameRva));
        diagnostics_.Event("CreatureFrameInputRouterReady", detail);
        return true;
#endif
    }

    bool CreatureFrameInputRouterHook::Bind(
        void* sourcePlayerCreature,
        void* targetPhysicsNavigator)
    {
        if (!IsInstalled() ||
            !::fable::game::creature::native::CreatureFrameFunctions::ValidatePlayerCreature(
                gameModule_,
                sourcePlayerCreature) ||
            !native::PhysicsNavigatorFunctions::ValidateNavigator(
                gameModule_,
                targetPhysicsNavigator))
        {
            return false;
        }

        targetNavigator_.store(nullptr, std::memory_order_release);
        routedFrameCount_.store(0, std::memory_order_release);
        source_.store(sourcePlayerCreature, std::memory_order_release);
        targetNavigator_.store(
            targetPhysicsNavigator,
            std::memory_order_release);

        char detail[192] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "source_player=%p target_navigator=%p",
            sourcePlayerCreature,
            targetPhysicsNavigator);
        diagnostics_.Event("CreatureFrameInputRouterBound", detail);
        return true;
    }

    void CreatureFrameInputRouterHook::Clear() noexcept
    {
        targetNavigator_.store(nullptr, std::memory_order_release);
        source_.store(nullptr, std::memory_order_release);
    }

    void CreatureFrameInputRouterHook::SetFrameObserver(
        FrameObserver observer,
        void* context) noexcept
    {
        if (observer == nullptr)
        {
            frameObserver_.store(nullptr, std::memory_order_release);
            frameObserverContext_.store(nullptr, std::memory_order_release);
            return;
        }
        frameObserverContext_.store(context, std::memory_order_release);
        frameObserver_.store(observer, std::memory_order_release);
    }

    bool CreatureFrameInputRouterHook::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr && vtableSlot_ != nullptr;
    }

    bool CreatureFrameInputRouterHook::IsBound() const noexcept
    {
        return source_.load(std::memory_order_acquire) != nullptr &&
            targetNavigator_.load(std::memory_order_acquire) != nullptr;
    }

    unsigned int CreatureFrameInputRouterHook::RoutedFrameCount() const noexcept
    {
        return routedFrameCount_.load(std::memory_order_acquire);
    }

    bool __fastcall CreatureFrameInputRouterHook::ObservePlayerUpdate(
        void* playerCreature,
        void*)
    {
        CreatureFrameInputRouterHook* const router = active_;
        if (router == nullptr || router->original_ == nullptr)
        {
            return false;
        }

        const bool result = router->original_(playerCreature);
        const FrameObserver observer = router->frameObserver_.load(
            std::memory_order_acquire);
        if (observer != nullptr)
        {
            observer(
                router->frameObserverContext_.load(std::memory_order_acquire),
                playerCreature);
        }
        if (playerCreature != router->source_.load(std::memory_order_acquire))
        {
            return result;
        }

        void* const targetNavigator = router->targetNavigator_.load(
            std::memory_order_acquire);
        if (targetNavigator == nullptr)
        {
            return result;
        }

        Vector3 frameMotion = {};
        Vector3 currentTargetPosition = {};
        bool readable = false;
        __try
        {
            const auto* const sourceBytes = static_cast<const std::uint8_t*>(
                playerCreature);
            const auto* const targetBytes = static_cast<const std::uint8_t*>(
                targetNavigator);
            std::memcpy(
                &frameMotion,
                sourceBytes + ::fable::game::creature::native::CreatureFrameFunctions::MotionXOffset,
                sizeof(frameMotion));
            std::memcpy(
                &currentTargetPosition,
                targetBytes + native::PhysicsNavigatorFunctions::WorldPositionOffset,
                sizeof(currentTargetPosition));
            readable = std::isfinite(frameMotion.x) &&
                std::isfinite(frameMotion.y) &&
                std::isfinite(frameMotion.z) &&
                std::isfinite(currentTargetPosition.x) &&
                std::isfinite(currentTargetPosition.y) &&
                std::isfinite(currentTargetPosition.z);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }

        const float motionSquared = frameMotion.x * frameMotion.x +
            frameMotion.y * frameMotion.y + frameMotion.z * frameMotion.z;
        if (!readable || motionSquared <= 0.00000001f)
        {
            return result;
        }

        const Vector3 desiredPosition = {
            currentTargetPosition.x + frameMotion.x,
            currentTargetPosition.y + frameMotion.y,
            currentTargetPosition.z + frameMotion.z,
        };
        if (!native::PhysicsNavigatorFunctions::RequestNextPosition(
                router->gameModule_,
                targetNavigator,
                desiredPosition))
        {
            return result;
        }

        const unsigned int ordinal = router->routedFrameCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;
        if (ordinal == 1 || ordinal == 2 || ordinal == 10 || ordinal == 60)
        {
            char detail[384] = {};
            std::snprintf(
                detail,
                std::size(detail),
                "ordinal=%u source_player=%p target_navigator=%p frame_motion=(%.6f,%.6f,%.6f) target_current=(%.3f,%.3f,%.3f) requested=(%.3f,%.3f,%.3f) thread=%lu",
                ordinal,
                playerCreature,
                targetNavigator,
                frameMotion.x,
                frameMotion.y,
                frameMotion.z,
                currentTargetPosition.x,
                currentTargetPosition.y,
                currentTargetPosition.z,
                desiredPosition.x,
                desiredPosition.y,
                desiredPosition.z,
                static_cast<unsigned long>(GetCurrentThreadId()));
            router->diagnostics_.Event("CreatureFrameInputRouted", detail);
        }
        return result;
    }
}
