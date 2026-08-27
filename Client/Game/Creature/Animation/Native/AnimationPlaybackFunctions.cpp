#include "AnimationPlaybackFunctions.h"

#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/Native/ThingComponentAccess.h"

#include <atomic>
#include <cstring>

namespace
{
    constexpr std::array<std::uint8_t, 8> kConstructRequestPrefix = {
        0x8B, 0x44, 0x24, 0x10, 0x8A, 0x54, 0x24, 0x04,
    };
    constexpr std::array<std::uint8_t, 3> kSehPrefix = {
        0x6A, 0xFF, 0x68,
    };
    constexpr std::array<std::uint8_t, 9> kValidateAnimationPrefix = {
        0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x7E, 0x19,
    };
    std::atomic<HMODULE> gPristineValidatedModule{nullptr};
}

namespace fable::game::creature::animation::native
{
    bool AnimationPlaybackFunctions::Resolve(HMODULE gameModule) noexcept
    {
        gameModule_ = nullptr;
        constructRequest_ = nullptr;
        submitRequest_ = nullptr;
        validateAnimation_ = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const construct = reinterpret_cast<const std::uint8_t*>(
            base + ConstructRequestRva);
        auto* const submit = reinterpret_cast<const std::uint8_t*>(
            base + SubmitRequestRva);
        auto* const validate = reinterpret_cast<const std::uint8_t*>(
            base + ValidateAnimationRva);
        std::uintptr_t submitExceptionHandler = 0;
        bool pristineSubmit = false;
        __try
        {
            pristineSubmit = std::memcmp(
                submit, kSehPrefix.data(), kSehPrefix.size()) == 0;
            const bool knownModuleDetour = submit[0] == 0xE9 &&
                gPristineValidatedModule.load(std::memory_order_acquire) ==
                    gameModule;
            if (std::memcmp(
                    construct,
                    kConstructRequestPrefix.data(),
                    kConstructRequestPrefix.size()) != 0 ||
                (!pristineSubmit && !knownModuleDetour) ||
                std::memcmp(
                    validate,
                    kValidateAnimationPrefix.data(),
                    kValidateAnimationPrefix.size()) != 0)
            {
                return false;
            }
            if (pristineSubmit)
            {
                std::memcpy(
                    &submitExceptionHandler,
                    submit + kSehPrefix.size(),
                    sizeof(submitExceptionHandler));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        if (pristineSubmit && submitExceptionHandler !=
                base + SubmitRequestExceptionHandlerRva)
        {
            return false;
        }
        if (pristineSubmit)
        {
            gPristineValidatedModule.store(
                gameModule, std::memory_order_release);
        }

        gameModule_ = gameModule;
        constructRequest_ = reinterpret_cast<ConstructRequestPointer>(
            const_cast<std::uint8_t*>(construct));
        submitRequest_ = reinterpret_cast<SubmitRequestPointer>(
            const_cast<std::uint8_t*>(submit));
        validateAnimation_ = reinterpret_cast<ValidateAnimationPointer>(
            const_cast<std::uint8_t*>(validate));
        return true;
    }

    AnimationPlaybackAttempt AnimationPlaybackFunctions::Play(
        void* creature,
        std::uint32_t animationId,
        std::uint32_t flags) const noexcept
    {
        AnimationPlaybackAttempt attempt;
        if (!IsResolved())
        {
            return attempt;
        }
        if (creature == nullptr || animationId == 0 ||
            animationId > MaximumAnimationId ||
            (flags & ~SupportedFlags) != 0)
        {
            attempt.result = AnimationPlaybackResult::InvalidRequest;
            return attempt;
        }
        if (!::fable::game::creature::native::CreatureFrameFunctions::
                ValidateCreature(gameModule_, creature) &&
            !::fable::game::creature::native::CreatureFrameFunctions::
                ValidatePlayerCreature(gameModule_, creature))
        {
            attempt.result = AnimationPlaybackResult::InvalidCreature;
            return attempt;
        }

        attempt.animationComplex =
            entity::native::ThingComponentAccess::Find(
                creature,
                entity::native::ThingComponentType::AnimationComplex);
        if (attempt.animationComplex == nullptr)
        {
            attempt.result =
                AnimationPlaybackResult::MissingAnimationComplex;
            return attempt;
        }

        bool validAnimation = false;
        __try
        {
            attempt.animationVtable =
                *static_cast<void**>(attempt.animationComplex);
            attempt.animationState = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(attempt.animationComplex) +
                AnimationStateOffset);
            validAnimation = validateAnimation_(
                creature, static_cast<std::int32_t>(animationId));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            attempt.result = AnimationPlaybackResult::InspectionFault;
            return attempt;
        }
        if (reinterpret_cast<std::uintptr_t>(attempt.animationVtable) !=
            reinterpret_cast<std::uintptr_t>(gameModule_) +
                AnimationComplexVtableRva)
        {
            attempt.result =
                AnimationPlaybackResult::UnexpectedAnimationComplex;
            return attempt;
        }
        if (attempt.animationState == nullptr)
        {
            attempt.result = AnimationPlaybackResult::MissingAnimationState;
            return attempt;
        }
        // CreatureAction_PlayResolvedAnimation does not reject when this
        // resource-class probe is false. It sets bit 1 on the animation state
        // (+0x59) and continues into the same request submission path.
        if (!validAnimation)
        {
            __try
            {
                *reinterpret_cast<std::uint8_t*>(
                    static_cast<std::uint8_t*>(attempt.animationState) +
                    ResourceClassFallbackStateOffset) |=
                        ResourceClassFallbackStateFlag;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                attempt.result =
                    AnimationPlaybackResult::StatePreparationFault;
                return attempt;
            }
        }

        AnimationPlaybackRequest request = {};
        __try
        {
            constructRequest_(
                &request,
                1,
                0,
                static_cast<std::int32_t>(animationId),
                0,
                0,
                flags,
                100);
            submitRequest_(attempt.animationState, &request, 1, 0);
            attempt.result = AnimationPlaybackResult::Played;
            return attempt;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            attempt.result = AnimationPlaybackResult::SubmissionFault;
            return attempt;
        }
    }

    const char* AnimationPlaybackFunctions::ResultName(
        AnimationPlaybackResult result) noexcept
    {
        switch (result)
        {
        case AnimationPlaybackResult::Played:
            return "played";
        case AnimationPlaybackResult::NotReady:
            return "not-ready";
        case AnimationPlaybackResult::InvalidRequest:
            return "invalid-request";
        case AnimationPlaybackResult::InvalidCreature:
            return "invalid-creature";
        case AnimationPlaybackResult::MissingAnimationComplex:
            return "missing-animation-complex";
        case AnimationPlaybackResult::InspectionFault:
            return "inspection-fault";
        case AnimationPlaybackResult::UnexpectedAnimationComplex:
            return "unexpected-animation-complex";
        case AnimationPlaybackResult::MissingAnimationState:
            return "missing-animation-state";
        case AnimationPlaybackResult::StatePreparationFault:
            return "state-preparation-fault";
        case AnimationPlaybackResult::SubmissionFault:
            return "submission-fault";
        default:
            return "unknown";
        }
    }

    bool AnimationPlaybackFunctions::IsResolved() const noexcept
    {
        return gameModule_ != nullptr && constructRequest_ != nullptr &&
            submitRequest_ != nullptr && validateAnimation_ != nullptr;
    }
}
