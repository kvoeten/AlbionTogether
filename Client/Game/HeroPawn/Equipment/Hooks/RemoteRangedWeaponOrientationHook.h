#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fable::game::hero_pawn::equipment::hooks
{
    // Fable resolves an attachment's final transform through two paths: the
    // ordinary CTCCarrying path and the animation-driven solid-to-thing path
    // used while aiming. Remote Hero bows use the correct hand slot but receive
    // the opposite forward basis. This hook corrects only explicitly
    // registered, active remote ranged weapons at their common final seam.
    class RemoteRangedWeaponOrientationHook final
    {
    public:
        using RegistrationToken = std::uint32_t;

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;

        [[nodiscard]] RegistrationToken Register(
            void* carryingComponent,
            std::uint64_t actorId) noexcept;
        [[nodiscard]] bool SetActiveWeapon(
            RegistrationToken token,
            void* rangedWeapon) noexcept;
        void Unregister(RegistrationToken token) noexcept;
        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        struct NativeRightHandedSet final
        {
            float up[3] = {};
            float forward[3] = {};
        };
        static_assert(sizeof(NativeRightHandedSet) == 24);

        using ResolveTransform = bool(__thiscall*)(
            void* attachment,
            float* position,
            NativeRightHandedSet* orientation,
            bool interpolate,
            float alpha,
            bool useSolidAttachment);

        static constexpr std::size_t RegistrationCapacity = 64;
        static constexpr RegistrationToken ReservedToken =
            (std::numeric_limits<RegistrationToken>::max)();

        struct Registration final
        {
            std::atomic<RegistrationToken> token{0};
            std::atomic<void*> carrying{nullptr};
            std::atomic<void*> weapon{nullptr};
            std::atomic<std::uint64_t> actorId{0};
            std::atomic_bool correctionReported{false};
        };

        static bool __fastcall ResolveRemoteTransform(
            void* attachment,
            void* unused,
            float* position,
            NativeRightHandedSet* orientation,
            bool interpolate,
            float alpha,
            bool useSolidAttachment);
        [[nodiscard]] Registration* FindAttachmentMatch(
            void* attachment) noexcept;
        [[nodiscard]] Registration* FindMatch(
            void* carryingComponent,
            void* carriedThing) noexcept;
        [[nodiscard]] RegistrationToken NextToken() noexcept;
        void ReportCorrection(Registration& registration) noexcept;
        bool InstallDetour(
            std::uint8_t* target,
            void* replacement) noexcept;

        static RemoteRangedWeaponOrientationHook* active_;
        core::Diagnostics diagnostics_ = {};
        ResolveTransform original_ = nullptr;
        core::hooking::InlineHook patch_;
        std::array<Registration, RegistrationCapacity> registrations_ = {};
        std::atomic<RegistrationToken> nextToken_{1};
    };
}
