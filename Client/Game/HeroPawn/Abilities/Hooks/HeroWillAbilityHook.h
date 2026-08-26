#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/HeroPawn/Abilities/HeroAbility.h"
#include "Game/HeroPawn/Abilities/Native/HeroWillAbilityFunctions.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::hero_pawn::abilities
{
    class HeroWillAbilityService;
}

namespace fable::game::hero_pawn::abilities::hooks
{
    class HeroWillAbilityHook final
    {
    public:
        bool Install(
            HMODULE gameModule,
            HeroWillAbilityService& service,
            const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] bool SubmitReplicated(
            void* component,
            HeroAbility ability,
            HeroAbilityCommand command) noexcept;
        [[nodiscard]] bool SubmitLocal(
            void* component,
            HeroAbility ability,
            HeroAbilityCommand command) noexcept;

    private:
        struct Detour final
        {
            bool Install(
                std::uint8_t* targetAddress,
                void* replacement,
                std::size_t bytes =
                    native::HeroWillAbilityFunctions::DisplacedBytes) noexcept;
            [[nodiscard]] bool Reset() noexcept;
            [[nodiscard]] bool IsInstalled() const noexcept
            {
                return patch.IsInstalled();
            }
            [[nodiscard]] void* Original() const noexcept
            {
                return patch.Original();
            }

            core::hooking::InlineHook patch;
        };

        static bool __fastcall InterceptUse(
            void* component,
            void* unused,
            HeroAbility ability);
        static bool __fastcall InterceptToggle(
            void* component,
            void* unused,
            HeroAbility ability);
        static bool __fastcall InterceptCancel(
            void* component,
            void* unused,
            HeroAbility ability);
        static bool __fastcall InterceptEligibility(
            void* component,
            void* unused,
            HeroAbility ability);
        static void __fastcall InterceptTurncoatState(
            void* state,
            void* unused,
            float amount);
        static bool Intercept(
            void* component,
            HeroAbility ability,
            HeroAbilityCommand command) noexcept;
        static bool InvokeCommandSafely(
            native::HeroWillAbilityFunctions::CommandPointer command,
            void* component,
            HeroAbility ability) noexcept;
        static bool InvokeTurncoatStateSafely(
            native::HeroWillAbilityFunctions::TurncoatStatePointer command,
            void* state,
            float amount) noexcept;
        [[nodiscard]] bool Submit(
            void* component,
            HeroAbility ability,
            HeroAbilityCommand command) noexcept;

        static HeroWillAbilityHook* active_;

        HeroWillAbilityService* service_ = nullptr;
        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        Detour use_;
        Detour toggle_;
        Detour cancel_;
        Detour eligibility_;
        Detour turncoatState_;
        native::HeroWillAbilityFunctions::CommandPointer originalUse_ = nullptr;
        native::HeroWillAbilityFunctions::CommandPointer originalToggle_ = nullptr;
        native::HeroWillAbilityFunctions::CommandPointer originalCancel_ = nullptr;
        native::HeroWillAbilityFunctions::CommandPointer originalEligibility_ =
            nullptr;
        native::HeroWillAbilityFunctions::TurncoatStatePointer
            originalTurncoatState_ = nullptr;
    };
}
