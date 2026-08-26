#include "RemoteHeroPresentationFactoryHook.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr std::uintptr_t kPresentationFactoryRva = 0x01C50850;
    constexpr std::size_t kPrologueSize = 7;
    constexpr std::uint32_t kHeroPresentationSelector = 1;
    constexpr float kPositionTolerance = 0.05f;
    constexpr std::uint64_t kArmLifetimeMilliseconds = 10'000;
    constexpr std::uint32_t kMaximumObservedCommands = 8;

    struct PromotionResult final
    {
        void* payload = nullptr;
        fable::game::Vector3 position = {};
        std::uint32_t originalSelector = 0;
        bool observed = false;
        bool faulted = false;
        bool promoted = false;
    };

    std::uint32_t FloatBits(float value) noexcept
    {
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    float BitsFloat(std::uint32_t bits) noexcept
    {
        float value = 0.0f;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    bool PositionMatches(
        const fable::game::Vector3& actual,
        const fable::game::Vector3& expected) noexcept
    {
        return std::isfinite(actual.x) &&
            std::isfinite(actual.y) &&
            std::isfinite(actual.z) &&
            std::fabs(actual.x - expected.x) <= kPositionTolerance &&
            std::fabs(actual.y - expected.y) <= kPositionTolerance &&
            std::fabs(actual.z - expected.z) <= kPositionTolerance;
    }

}

namespace fable::game::hero_pawn::appearance::hooks
{
    static_assert(sizeof(void*) == 4);

    RemoteHeroPresentationFactoryHook*
        RemoteHeroPresentationFactoryHook::active_ = nullptr;

    bool RemoteHeroPresentationFactoryHook::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics) noexcept
    {
        diagnostics_ = diagnostics;
        if (IsInstalled())
        {
            return true;
        }
        if (gameModule == nullptr ||
            (active_ != nullptr && active_ != this))
        {
            return false;
        }

        auto* const target = reinterpret_cast<std::uint8_t*>(gameModule) +
            kPresentationFactoryRva;
        constexpr std::uint8_t kExpectedPrologue[] = {0x6A, 0xFF, 0x68};
        if (std::memcmp(
                target,
                kExpectedPrologue,
                sizeof(kExpectedPrologue)) != 0)
        {
            return false;
        }
        constexpr std::uint8_t expectedPrologue[] = {0x6A, 0xFF, 0x68};
        if (!patch_.Install(
                target,
                expectedPrologue,
                sizeof(expectedPrologue),
                reinterpret_cast<void*>(
                    &RemoteHeroPresentationFactoryHook::CreatePresentation),
                kPrologueSize))
        {
            return false;
        }
        original_ = reinterpret_cast<FactoryFunction>(patch_.Original());
        active_ = this;
        diagnostics_.Log(
            "Hook: remote Hero presentation factory promotion installed.");
        diagnostics_.Event(
            "MultiplayerRemoteHeroPresentationFactoryReady",
            "native create-presentation command can promote one scoped AI pawn to the complete AHeroPawn initialization path");
        return true;
    }

    void RemoteHeroPresentationFactoryHook::Shutdown() noexcept
    {
        if (!patch_.Shutdown())
        {
            diagnostics_.Log(
                "Hook: remote Hero presentation patch was not removed because target ownership changed.");
            return;
        }
        Cancel();
        if (active_ == this)
        {
            active_ = nullptr;
        }
        original_ = nullptr;
        expectedX_.store(0, std::memory_order_relaxed);
        expectedY_.store(0, std::memory_order_relaxed);
        expectedZ_.store(0, std::memory_order_relaxed);
        expectedGraphic_.store(0, std::memory_order_relaxed);
        observations_.store(0, std::memory_order_relaxed);
        expiresAt_.store(0, std::memory_order_relaxed);
        armToken_.store(0, std::memory_order_relaxed);
        armed_.store(false, std::memory_order_release);
        diagnostics_ = {};
    }

    RemoteHeroPresentationFactoryHook::ArmToken
        RemoteHeroPresentationFactoryHook::Arm(
        const game::Vector3& expectedPosition) noexcept
    {
        const ArmToken token = armToken_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        expectedX_.store(FloatBits(expectedPosition.x), std::memory_order_relaxed);
        expectedY_.store(FloatBits(expectedPosition.y), std::memory_order_relaxed);
        expectedZ_.store(FloatBits(expectedPosition.z), std::memory_order_relaxed);
        expectedGraphic_.store(0, std::memory_order_relaxed);
        observations_.store(0, std::memory_order_relaxed);
        expiresAt_.store(
            GetTickCount64() + kArmLifetimeMilliseconds,
            std::memory_order_relaxed);
        armed_.store(true, std::memory_order_release);

        char detail[160] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "position=(%.3f,%.3f,%.3f) lifetime_ms=%llu",
            expectedPosition.x,
            expectedPosition.y,
            expectedPosition.z,
            static_cast<unsigned long long>(kArmLifetimeMilliseconds));
        diagnostics_.Event(
            "MultiplayerRemoteHeroPresentationArmed",
            detail);
        return token;
    }

    void RemoteHeroPresentationFactoryHook::TargetGraphic(
        ArmToken token,
        void* graphic) noexcept
    {
        if (token == 0 ||
            token != armToken_.load(std::memory_order_acquire))
        {
            return;
        }
        expectedGraphic_.store(
            reinterpret_cast<std::uintptr_t>(graphic),
            std::memory_order_release);
        char detail[96] = {};
        std::snprintf(detail, sizeof(detail), "graphic=%p", graphic);
        diagnostics_.Event(
            "MultiplayerRemoteHeroPresentationTargeted",
            detail);
    }

    void RemoteHeroPresentationFactoryHook::Cancel(ArmToken token) noexcept
    {
        if (token != 0 &&
            token != armToken_.load(std::memory_order_acquire))
        {
            return;
        }
        armed_.store(false, std::memory_order_release);
        expectedGraphic_.store(0, std::memory_order_relaxed);
        expiresAt_.store(0, std::memory_order_relaxed);
    }

    bool RemoteHeroPresentationFactoryHook::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr && patch_.IsInstalled();
    }

    bool RemoteHeroPresentationFactoryHook::IsArmed() const noexcept
    {
        return armed_.load(std::memory_order_acquire);
    }

    void __cdecl RemoteHeroPresentationFactoryHook::CreatePresentation(
        void* command,
        void* definitionName)
    {
        RemoteHeroPresentationFactoryHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            return;
        }

        PromotionResult result;
        if (hook->armed_.load(std::memory_order_acquire))
        {
            const std::uint64_t now = GetTickCount64();
            const std::uint64_t expiresAt = hook->expiresAt_.load(
                std::memory_order_relaxed);
            if (expiresAt == 0 || now > expiresAt)
            {
                hook->Cancel();
            }
            else
            {
                const game::Vector3 expected = {
                    BitsFloat(hook->expectedX_.load(std::memory_order_relaxed)),
                    BitsFloat(hook->expectedY_.load(std::memory_order_relaxed)),
                    BitsFloat(hook->expectedZ_.load(std::memory_order_relaxed))};
                __try
                {
                    auto* const commandBytes = static_cast<std::uint8_t*>(command);
                    result.payload = command == nullptr
                        ? nullptr
                        : *reinterpret_cast<void**>(commandBytes);
                    if (result.payload != nullptr)
                    {
                        result.position = *reinterpret_cast<game::Vector3*>(
                            commandBytes + 4);
                        auto* const selector = reinterpret_cast<std::uint32_t*>(
                            static_cast<std::uint8_t*>(result.payload) + 0x24);
                        result.originalSelector = *selector;
                        result.observed = true;
                        const auto expectedGraphic =
                            hook->expectedGraphic_.load(
                                std::memory_order_acquire);
                        const bool graphicMatches = expectedGraphic != 0 &&
                            reinterpret_cast<std::uintptr_t>(result.payload) ==
                                expectedGraphic;
                        if ((graphicMatches ||
                                PositionMatches(result.position, expected)) &&
                            result.originalSelector != kHeroPresentationSelector)
                        {
                            bool armed = true;
                            if (hook->armed_.compare_exchange_strong(
                                    armed,
                                    false,
                                    std::memory_order_acq_rel,
                                    std::memory_order_acquire))
                            {
                                *selector = kHeroPresentationSelector;
                                result.promoted = true;
                            }
                        }
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    result.faulted = true;
                }
            }
        }

        if ((result.observed || result.faulted) &&
            hook->observations_.fetch_add(
                1, std::memory_order_relaxed) < kMaximumObservedCommands)
        {
            const game::Vector3 expected = {
                BitsFloat(hook->expectedX_.load(std::memory_order_relaxed)),
                BitsFloat(hook->expectedY_.load(std::memory_order_relaxed)),
                BitsFloat(hook->expectedZ_.load(std::memory_order_relaxed))};
            const auto expectedGraphic = hook->expectedGraphic_.load(
                std::memory_order_acquire);
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "command=%p payload=%p expected_graphic=%p actual=(%.3f,%.3f,%.3f) expected_fable=(%.3f,%.3f,%.3f) selector=%u faulted=%s matched=%s",
                command,
                result.payload,
                reinterpret_cast<void*>(expectedGraphic),
                result.position.x,
                result.position.y,
                result.position.z,
                expected.x,
                expected.y,
                expected.z,
                result.originalSelector,
                result.faulted ? "true" : "false",
                result.promoted ? "true" : "false");
            hook->diagnostics_.Event(
                "MultiplayerRemoteHeroPresentationObserved",
                detail);
        }

        if (result.promoted)
        {
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "command=%p payload=%p position=(%.3f,%.3f,%.3f) selector=%u->%u actor=AI-entity presentation=AHeroPawn",
                command,
                result.payload,
                result.position.x,
                result.position.y,
                result.position.z,
                result.originalSelector,
                kHeroPresentationSelector);
            hook->diagnostics_.Log(
                "Multiplayer: promoted remote AI creature presentation to the native AHeroPawn factory path.");
            hook->diagnostics_.Event(
                "MultiplayerRemoteHeroPresentationPromoted",
                detail);
        }

        hook->original_(command, definitionName);
    }
}
