#pragma once

#include "CreatureWeaponFunctions.h"

namespace fable::game
{
    class EntityService;
}

namespace fable::game::creature::equipment::native
{
    // Keeps the two replicated Hero weapon Things alive independently from
    // their visible CTCCarrying attachment entries. A slot-zero weapon can
    // therefore remain loaded but hidden, then be attached without creating
    // its Thing or render graph on the first frame of a draw action.
    class CreatureWeaponCache final
    {
    public:
        [[nodiscard]] bool Ensure(
            game::EntityService& entities,
            void* creature,
            std::int32_t meleeDefinitionIndex,
            std::int32_t rangedDefinitionIndex) noexcept;
        [[nodiscard]] bool StageTransition(
            void* creature,
            CreatureWeaponFamily targetFamily) noexcept;
        [[nodiscard]] bool ApplyPresentation(
            void* creature,
            std::uint32_t meleeAttachmentSlot,
            std::uint32_t rangedAttachmentSlot,
            CreatureWeaponFamily activeFamily,
            CreatureWeaponInspection* inspection = nullptr) noexcept;
        [[nodiscard]] bool IsReady(
            void* creature,
            std::int32_t meleeDefinitionIndex,
            std::int32_t rangedDefinitionIndex) const noexcept;
        void Reset() noexcept;

        [[nodiscard]] std::int32_t MeleeDefinitionIndex() const noexcept
        {
            return meleeDefinitionIndex_;
        }

        [[nodiscard]] std::int32_t RangedDefinitionIndex() const noexcept
        {
            return rangedDefinitionIndex_;
        }

    private:
        struct NativeThingReference final
        {
            void* vtable = nullptr;
            void* control = nullptr;
        };

        static_assert(
            sizeof(NativeThingReference) == sizeof(void*) * 2,
            "Unexpected Fable intelligent-pointer layout.");

        void* creature_ = nullptr;
        fable::game::EntityService* entities_ = nullptr;
        std::int32_t meleeDefinitionIndex_ = -1;
        std::int32_t rangedDefinitionIndex_ = -1;
        NativeThingReference melee_ = {};
        NativeThingReference ranged_ = {};
        bool meleeHidden_ = false;
        bool rangedHidden_ = false;

        [[nodiscard]] bool HideWeapon(void* weapon) noexcept;
        [[nodiscard]] bool ShowAttachedWeapon(void* weapon) noexcept;
    };
}
