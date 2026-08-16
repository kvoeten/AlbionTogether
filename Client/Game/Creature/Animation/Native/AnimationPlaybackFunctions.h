#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::animation::native
{
    struct AnimationPlaybackRequest final
    {
        std::int32_t channel = 0;
        std::uint8_t immediate = 0;
        std::uint8_t option = 0;
        std::uint16_t reserved = 0;
        std::int32_t animationId = 0;
        float playbackRate = 1.0f;
        float blendIn = 1.0f;
        float blendOut = 1.0f;
        float startTime = 0.0f;
        float endTime = 0.0f;
        std::uint32_t flags = 0;
        std::int32_t variant = 0;
        std::int32_t priority = 100;
    };

    static_assert(
        sizeof(AnimationPlaybackRequest) == 0x2C,
        "Unexpected Fable animation request layout.");

    enum class AnimationPlaybackResult : std::uint8_t
    {
        Played = 0,
        NotReady,
        InvalidRequest,
        InvalidCreature,
        MissingAnimationComplex,
        InspectionFault,
        UnexpectedAnimationComplex,
        MissingAnimationState,
        StatePreparationFault,
        SubmissionFault,
    };

    struct AnimationPlaybackAttempt final
    {
        AnimationPlaybackResult result = AnimationPlaybackResult::NotReady;
        void* animationComplex = nullptr;
        void* animationVtable = nullptr;
        void* animationState = nullptr;
    };

    class AnimationPlaybackFunctions final
    {
    public:
        static constexpr std::uintptr_t ConstructRequestRva = 0x01775F20;
        static constexpr std::uintptr_t SubmitRequestRva = 0x01779B70;
        static constexpr std::uintptr_t SubmitRequestExceptionHandlerRva =
            0x02559FA8;
        static constexpr std::uintptr_t ValidateAnimationRva = 0x01B3CC50;
        static constexpr std::uintptr_t AnimationComplexVtableRva =
            0x02AED3D4;
        static constexpr std::size_t AnimationStateOffset = 0x0C;
        static constexpr std::uint32_t MaximumAnimationId = 0xFFFF;
        static constexpr std::uint32_t SupportedFlags = 0x07;

        using ConstructRequestPointer = int(__thiscall*)(
            AnimationPlaybackRequest* request,
            std::uint8_t immediate,
            std::int32_t variant,
            std::int32_t animationId,
            std::int32_t channel,
            std::uint8_t option,
            std::uint32_t flags,
            std::int32_t priority);
        // Retail callers treat CTCAnimationComplex::SubmitRequest as void.
        // EAX is scratch state at return and is not an acceptance result.
        using SubmitRequestPointer = void(__thiscall*)(
            void* animationState,
            const AnimationPlaybackRequest* request,
            std::int32_t blendFrames,
            std::int32_t options);
        using ValidateAnimationPointer = bool(__thiscall*)(
            void* creature,
            std::int32_t animationId);

        bool Resolve(HMODULE gameModule) noexcept;
        AnimationPlaybackAttempt Play(
            void* creature,
            std::uint32_t animationId,
            std::uint32_t flags) const noexcept;
        [[nodiscard]] static const char* ResultName(
            AnimationPlaybackResult result) noexcept;
        [[nodiscard]] bool IsResolved() const noexcept;

    private:
        HMODULE gameModule_ = nullptr;
        ConstructRequestPointer constructRequest_ = nullptr;
        SubmitRequestPointer submitRequest_ = nullptr;
        ValidateAnimationPointer validateAnimation_ = nullptr;
    };
}
