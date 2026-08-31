#include "CreatureWeaponCache.h"

#include "CreatureWeaponNativeSupport.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"

#include <Windows.h>

namespace fable::game::creature::equipment::native::detail
{
    namespace
    {
        constexpr std::size_t ThingUidOffset = 0x14;
    }

    std::uint64_t ReadThingUid(void* thing) noexcept
    {
        if (thing == nullptr)
        {
            return 0;
        }
        std::uint64_t uid = 0;
        __try
        {
            uid = *reinterpret_cast<const std::uint64_t*>(
                static_cast<const std::uint8_t*>(thing) + ThingUidOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            uid = 0;
        }
        return uid;
    }
}

namespace fable::game::creature::equipment::native
{
    using namespace detail;

    bool CreatureWeaponCache::HideWeapon(void* weapon) noexcept
    {
        if (entities_ == nullptr || weapon == nullptr)
        {
            return false;
        }
        const std::uint64_t uid = ReadThingUid(weapon);
        if (uid == 0)
        {
            return false;
        }
        game::Entity* const entity = entities_->FindByUid(uid);
        if (entity == nullptr)
        {
            return false;
        }
        const bool collidable = entity->SetCollidable(false);
        const bool drawable = entity->SetDrawable(false);
        entity->Release();
        return collidable && drawable;
    }

    bool CreatureWeaponCache::ShowAttachedWeapon(void* weapon) noexcept
    {
        if (entities_ == nullptr || weapon == nullptr)
        {
            return false;
        }
        const std::uint64_t uid = ReadThingUid(weapon);
        if (uid == 0)
        {
            return false;
        }
        game::Entity* const entity = entities_->FindByUid(uid);
        if (entity == nullptr)
        {
            return false;
        }
        // Cached weapon Things are presentation owned by CTCCarrying. They
        // must never regain world-pickup collision when becoming visible.
        const bool collidable = entity->SetCollidable(false);
        const bool drawable = entity->SetDrawable(true);
        entity->Release();
        return drawable && collidable;
    }

    bool CreatureWeaponCache::Ensure(
        game::EntityService& entities,
        void* creature,
        std::int32_t meleeDefinitionIndex,
        std::int32_t rangedDefinitionIndex) noexcept
    {
        if (creature == nullptr || !IsSaneDefinition(meleeDefinitionIndex) ||
            !IsSaneDefinition(rangedDefinitionIndex))
        {
            return false;
        }

        if (creature_ != nullptr && creature_ != creature)
        {
            Reset();
        }
        entities_ = &entities;

        HMODULE const gameModule = GetModuleHandleW(nullptr);
        Functions functions;
        void* const carrying = FindCarrying(creature);
        std::uint32_t signatureMask = 0;
        if (carrying == nullptr ||
            !ResolveFunctions(gameModule, functions, &signatureMask) ||
            (signatureMask & PointerLifecycleFunctionMask) !=
                PointerLifecycleFunctionMask ||
            functions.initializePointer == nullptr ||
            functions.assignPointer == nullptr ||
            functions.destroyPointer == nullptr)
        {
            return false;
        }

        const auto prepareDetachedWeapon = [&, this](
            void* weapon,
            bool& hidden,
            const bool materialized)
        {
            std::uint32_t attachmentSlot = 0;
            const bool hasCarryingEntry = ReadAttachmentSlot(
                functions, carrying, weapon, attachmentSlot);
            if (materialized && hasCarryingEntry)
            {
                // EquipWeapon can insert a slot-zero CTCCarrying record before
                // the weapon receives its first visible attachment. Leaving
                // that record in place and attaching the retained Thing later
                // duplicates its ownership: one entry renders in-hand while
                // the slot-zero entry remains a world pickup. Remove every
                // newly materialized carrying record, including slot zero,
                // before parking the retained Thing in the hidden cache.
                functions.remove(carrying, weapon);
                hidden = HideWeapon(weapon);
                return hidden;
            }
            if (hasCarryingEntry && attachmentSlot != 0)
            {
                if (hidden)
                {
                    hidden = !ShowAttachedWeapon(weapon);
                    if (hidden)
                    {
                        return false;
                    }
                }
                return true;
            }
            hidden = HideWeapon(weapon);
            return hidden;
        };

        const auto ensureReference = [&](
            NativeThingReference& reference,
            std::int32_t& retainedDefinition,
            bool& hidden,
            const std::int32_t requestedDefinition,
            bool& materialized)
        {
            materialized = false;
            if (reference.vtable == nullptr)
            {
                functions.initializePointer(&reference);
            }

            void* retained = functions.resolvePointer(&reference);
            std::int32_t retainedThingDefinition = -1;
            ReadThingDefinition(retained, retainedThingDefinition);
            if (requestedDefinition <= 0)
            {
                functions.assignPointer(&reference, nullptr);
                retainedDefinition = -1;
                hidden = false;
                return true;
            }
            if (retained != nullptr &&
                retainedThingDefinition == requestedDefinition)
            {
                retainedDefinition = requestedDefinition;
                return prepareDetachedWeapon(retained, hidden, false);
            }

            functions.assignPointer(&reference, nullptr);
            retainedDefinition = -1;
            if (!Contains(functions, carrying, requestedDefinition))
            {
                functions.equip(creature, requestedDefinition, true);
                materialized = true;
            }
            void* const weapon = functions.find(
                carrying, requestedDefinition);
            if (weapon == nullptr)
            {
                return false;
            }
            functions.assignPointer(&reference, weapon);
            retained = functions.resolvePointer(&reference);
            ReadThingDefinition(retained, retainedThingDefinition);
            if (retained == nullptr ||
                retainedThingDefinition != requestedDefinition)
            {
                functions.assignPointer(&reference, nullptr);
                return false;
            }
            retainedDefinition = requestedDefinition;
            hidden = false;
            return prepareDetachedWeapon(retained, hidden, materialized);
        };

        bool ready = false;
        __try
        {
            creature_ = creature;
            bool meleeMaterialized = false;
            const bool meleeReady = ensureReference(
                melee_, meleeDefinitionIndex_, meleeHidden_,
                meleeDefinitionIndex, meleeMaterialized);
            if (meleeReady && !meleeMaterialized)
            {
                bool rangedMaterialized = false;
                const bool rangedReady = ensureReference(
                    ranged_, rangedDefinitionIndex_, rangedHidden_,
                    rangedDefinitionIndex, rangedMaterialized);
                ready = rangedReady && !rangedMaterialized;
            }
            // Populate at most one missing native Thing per attempt. This
            // prevents back-to-back EquipWeapon side effects from racing
            // their short-lived CThingSoundEmitter teardown.
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ready = false;
        }
        return ready && IsReady(
            creature, meleeDefinitionIndex, rangedDefinitionIndex);
    }

    bool CreatureWeaponCache::StageTransition(
        void* creature,
        CreatureWeaponFamily targetFamily) noexcept
    {
        if (creature == nullptr || creature != creature_ ||
            (targetFamily != CreatureWeaponFamily::None &&
             targetFamily != CreatureWeaponFamily::Melee &&
             targetFamily != CreatureWeaponFamily::Ranged))
        {
            return false;
        }
        if (targetFamily == CreatureWeaponFamily::None)
        {
            return true;
        }

        HMODULE const gameModule = GetModuleHandleW(nullptr);
        Functions functions;
        void* const carrying = FindCarrying(creature);
        if (carrying == nullptr || !ResolveFunctions(gameModule, functions))
        {
            return false;
        }
        const NativeThingReference& reference =
            targetFamily == CreatureWeaponFamily::Melee ? melee_ : ranged_;
        const std::int32_t expectedDefinition =
            targetFamily == CreatureWeaponFamily::Melee
                ? meleeDefinitionIndex_
                : rangedDefinitionIndex_;
        bool ready = false;
        __try
        {
            void* const weapon = functions.resolvePointer(
                const_cast<NativeThingReference*>(&reference));
            std::int32_t definition = -1;
            void* graphic = nullptr;
            ReadThingDefinition(weapon, definition);
            ReadWeaponPresentation(weapon, graphic);
            if (weapon != nullptr && graphic != nullptr &&
                definition == expectedDefinition)
            {
                std::uint32_t currentSlot = 0;
                ReadAttachmentSlot(
                    functions, carrying, weapon, currentSlot);
                ready = currentSlot != 0;
                if (!ready)
                {
                    const bool hidden = targetFamily ==
                        CreatureWeaponFamily::Melee
                        ? meleeHidden_
                        : rangedHidden_;
                    ready = hidden;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ready = false;
        }
        return ready;
    }

    bool CreatureWeaponCache::ApplyPresentation(
        void* creature,
        std::uint32_t meleeAttachmentSlot,
        std::uint32_t rangedAttachmentSlot,
        CreatureWeaponFamily activeFamily,
        CreatureWeaponInspection* inspection) noexcept
    {
        if (inspection != nullptr)
        {
            *inspection = {};
        }
        if (!IsReady(
                creature, meleeDefinitionIndex_, rangedDefinitionIndex_) ||
            (activeFamily != CreatureWeaponFamily::None &&
             activeFamily != CreatureWeaponFamily::Melee &&
             activeFamily != CreatureWeaponFamily::Ranged) ||
            (meleeDefinitionIndex_ <= 0 && meleeAttachmentSlot != 0) ||
            (rangedDefinitionIndex_ <= 0 && rangedAttachmentSlot != 0) ||
            (activeFamily == CreatureWeaponFamily::Melee &&
             (meleeDefinitionIndex_ <= 0 || meleeAttachmentSlot == 0)) ||
            (activeFamily == CreatureWeaponFamily::Ranged &&
             (rangedDefinitionIndex_ <= 0 || rangedAttachmentSlot == 0)))
        {
            return false;
        }

        HMODULE const gameModule = GetModuleHandleW(nullptr);
        Functions functions;
        void* const carrying = FindCarrying(creature);
        if (carrying == nullptr || !ResolveFunctions(gameModule, functions) ||
            (meleeAttachmentSlot != 0 &&
             !AttachmentSlotAvailable(functions, meleeAttachmentSlot)) ||
            (rangedAttachmentSlot != 0 &&
             !AttachmentSlotAvailable(functions, rangedAttachmentSlot)))
        {
            return false;
        }

        bool applied = false;
        __try
        {
            void* const meleeWeapon = functions.resolvePointer(&melee_);
            void* const rangedWeapon = functions.resolvePointer(&ranged_);
            std::uint32_t currentMeleeSlot = 0;
            std::uint32_t currentRangedSlot = 0;
            ReadAttachmentSlot(
                functions, carrying, meleeWeapon, currentMeleeSlot);
            ReadAttachmentSlot(
                functions, carrying, rangedWeapon, currentRangedSlot);

            const auto applyWeapon = [&](void* weapon,
                                         std::uint32_t currentSlot,
                                         std::uint32_t desiredSlot,
                                         bool& hidden)
            {
                if (weapon == nullptr)
                {
                    return desiredSlot == 0;
                }
                if (desiredSlot == 0)
                {
                    if (currentSlot != 0)
                    {
                        functions.remove(carrying, weapon);
                    }
                    hidden = HideWeapon(weapon);
                    return hidden;
                }
                if (currentSlot == desiredSlot)
                {
                    if (hidden)
                    {
                        hidden = !ShowAttachedWeapon(weapon);
                    }
                    return !hidden;
                }
                if (currentSlot != 0)
                {
                    functions.remove(carrying, weapon);
                }
                // Establish CTCCarrying ownership while the cached Thing is
                // still invisible and non-collidable. Showing it first leaves
                // a one-frame world pickup and uses the world-item transform.
                functions.attach(carrying, weapon, desiredSlot, true);
                if (hidden && !ShowAttachedWeapon(weapon))
                {
                    functions.remove(carrying, weapon);
                    return false;
                }
                hidden = false;
                return true;
            };

            const bool meleeApplied = meleeDefinitionIndex_ <= 0
                ? meleeAttachmentSlot == 0
                : applyWeapon(
                    meleeWeapon, currentMeleeSlot, meleeAttachmentSlot,
                    meleeHidden_);
            const bool rangedApplied = rangedDefinitionIndex_ <= 0
                ? rangedAttachmentSlot == 0
                : applyWeapon(
                    rangedWeapon, currentRangedSlot, rangedAttachmentSlot,
                    rangedHidden_);

            std::uint32_t verifiedMeleeSlot = 0;
            std::uint32_t verifiedRangedSlot = 0;
            ReadAttachmentSlot(
                functions, carrying, meleeWeapon, verifiedMeleeSlot);
            ReadAttachmentSlot(
                functions, carrying, rangedWeapon, verifiedRangedSlot);
            applied = meleeApplied && rangedApplied &&
                verifiedMeleeSlot == meleeAttachmentSlot &&
                verifiedRangedSlot == rangedAttachmentSlot;
            if (!applied)
            {
                if (meleeWeapon != nullptr &&
                    verifiedMeleeSlot != currentMeleeSlot)
                {
                    if (verifiedMeleeSlot != 0)
                    {
                        functions.remove(carrying, meleeWeapon);
                    }
                    if (currentMeleeSlot != 0)
                    {
                        functions.attach(
                            carrying, meleeWeapon, currentMeleeSlot, true);
                    }
                }
                if (rangedWeapon != nullptr &&
                    verifiedRangedSlot != currentRangedSlot)
                {
                    if (verifiedRangedSlot != 0)
                    {
                        functions.remove(carrying, rangedWeapon);
                    }
                    if (currentRangedSlot != 0)
                    {
                        functions.attach(
                            carrying, rangedWeapon, currentRangedSlot, true);
                    }
                }
                if (meleeWeapon != nullptr && currentMeleeSlot == 0)
                {
                    meleeHidden_ = HideWeapon(meleeWeapon);
                }
                if (rangedWeapon != nullptr && currentRangedSlot == 0)
                {
                    rangedHidden_ = HideWeapon(rangedWeapon);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }

        if (inspection != nullptr)
        {
            (void)CreatureWeaponFunctions::Inspect(
                creature,
                meleeDefinitionIndex_,
                rangedDefinitionIndex_,
                *inspection);
        }
        return applied;
    }

    bool CreatureWeaponCache::IsReady(
        void* creature,
        std::int32_t meleeDefinitionIndex,
        std::int32_t rangedDefinitionIndex) const noexcept
    {
        if (creature == nullptr || creature != creature_ ||
            meleeDefinitionIndex != meleeDefinitionIndex_ ||
            rangedDefinitionIndex != rangedDefinitionIndex_)
        {
            return false;
        }
        HMODULE const gameModule = GetModuleHandleW(nullptr);
        Functions functions;
        if (!ResolveFunctions(gameModule, functions))
        {
            return false;
        }
        bool ready = false;
        __try
        {
            void* const meleeWeapon = functions.resolvePointer(
                const_cast<NativeThingReference*>(&melee_));
            void* const rangedWeapon = functions.resolvePointer(
                const_cast<NativeThingReference*>(&ranged_));
            std::int32_t actualMeleeDefinition = -1;
            std::int32_t actualRangedDefinition = -1;
            ReadThingDefinition(meleeWeapon, actualMeleeDefinition);
            ReadThingDefinition(rangedWeapon, actualRangedDefinition);
            ready = (meleeDefinitionIndex <= 0 ||
                        actualMeleeDefinition == meleeDefinitionIndex) &&
                (rangedDefinitionIndex <= 0 ||
                    actualRangedDefinition == rangedDefinitionIndex);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ready = false;
        }
        return ready;
    }

    void CreatureWeaponCache::Reset() noexcept
    {
        HMODULE const gameModule = GetModuleHandleW(nullptr);
        Functions functions;
        const bool functionsResolved = ResolveFunctions(gameModule, functions);
        const auto release = [&](NativeThingReference& reference)
        {
            __try
            {
                if (reference.vtable != nullptr)
                {
                    if (functionsResolved &&
                        functions.destroyPointer != nullptr)
                    {
                        functions.destroyPointer(&reference);
                    }
                    else
                    {
                        ReleaseReference(&reference);
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
            reference = {};
        };
        release(melee_);
        release(ranged_);
        creature_ = nullptr;
        entities_ = nullptr;
        meleeDefinitionIndex_ = -1;
        rangedDefinitionIndex_ = -1;
        meleeHidden_ = false;
        rangedHidden_ = false;
    }
}
