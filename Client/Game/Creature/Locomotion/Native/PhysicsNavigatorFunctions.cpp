#include "PhysicsNavigatorFunctions.h"

#include <array>
#include <cmath>
#include <cstring>

namespace fable::game::creature::locomotion::native
{
    bool PhysicsNavigatorFunctions::ResolveSlots(
        HMODULE gameModule,
        void*** requestSlot,
        RequestNextPositionPointer& request,
        void*** updateSlot,
        UpdateMovementPointer& update) noexcept
    {
        if (requestSlot != nullptr)
        {
            *requestSlot = nullptr;
        }
        if (updateSlot != nullptr)
        {
            *updateSlot = nullptr;
        }
        request = nullptr;
        update = nullptr;
        if (gameModule == nullptr || requestSlot == nullptr || updateSlot == nullptr)
        {
            return false;
        }

        constexpr std::array<std::uint8_t, 7> requestPrefix = {
            0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x8B, 0x90,
        };
        constexpr std::array<std::uint8_t, 3> updatePrefix = {
            0x6A, 0xFF, 0x68,
        };
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto** const vtable = reinterpret_cast<void**>(base + VtableRva);
        auto** const candidateRequestSlot = vtable + RequestNextPositionSlot;
        auto** const candidateUpdateSlot = vtable + UpdateMovementSlot;
        const auto candidateRequest = reinterpret_cast<RequestNextPositionPointer>(
            base + RequestNextPositionRva);
        const auto candidateUpdate = reinterpret_cast<UpdateMovementPointer>(
            base + UpdateMovementRva);
        bool valid = false;
        __try
        {
            valid = *candidateRequestSlot == reinterpret_cast<void*>(candidateRequest) &&
                *candidateUpdateSlot == reinterpret_cast<void*>(candidateUpdate) &&
                std::memcmp(
                    reinterpret_cast<const void*>(candidateRequest),
                    requestPrefix.data(),
                    requestPrefix.size()) == 0 &&
                std::memcmp(
                    reinterpret_cast<const void*>(candidateUpdate),
                    updatePrefix.data(),
                    updatePrefix.size()) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            return false;
        }

        *requestSlot = candidateRequestSlot;
        *updateSlot = candidateUpdateSlot;
        request = candidateRequest;
        update = candidateUpdate;
        return true;
    }

    bool PhysicsNavigatorFunctions::ValidateNavigator(
        HMODULE gameModule,
        void* navigator) noexcept
    {
        if (gameModule == nullptr || navigator == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        bool valid = false;
        __try
        {
            valid = *static_cast<void**>(navigator) ==
                reinterpret_cast<void*>(base + VtableRva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        return valid;
    }

    bool PhysicsNavigatorFunctions::RequestNextPosition(
        HMODULE gameModule,
        void* navigator,
        const Vector3& desiredPosition) noexcept
    {
        if (!ValidateNavigator(gameModule, navigator) ||
            !std::isfinite(desiredPosition.x) ||
            !std::isfinite(desiredPosition.y) ||
            !std::isfinite(desiredPosition.z))
        {
            return false;
        }

        bool requested = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(navigator);
            const auto request = reinterpret_cast<RequestNextPositionPointer>(
                vtable[RequestNextPositionSlot]);
            request(navigator, &desiredPosition);
            requested = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            requested = false;
        }
        return requested;
    }
}
