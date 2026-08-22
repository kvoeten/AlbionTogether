#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Combat/Native/CreatureAbilitySubmissionFunction.h"

#include <Windows.h>

#include <atomic>

namespace fable::game::creature::combat
{
    class CreatureCombatService;

    class PlayerAttackAbilityHook final
    {
    public:
        bool Install(
            HMODULE gameModule,
            CreatureCombatService& service,
            const core::Diagnostics& diagnostics);

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] unsigned int InterceptedAttackCount() const noexcept;
        bool SubmitReplicatedAbility(
            void* creature,
            unsigned int abilityId,
            float charge) noexcept;

    private:
        static void __fastcall Intercept(
            void* creature,
            void* unused,
            unsigned int abilityId,
            float charge);

        static PlayerAttackAbilityHook* active_;

        CreatureCombatService* service_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        HMODULE gameModule_ = nullptr;
        native::CreatureAbilitySubmissionFunction::Pointer original_ = nullptr;
        void* trampoline_ = nullptr;
        std::atomic_uint interceptedAttackCount_{0};
    };
}
