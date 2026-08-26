#include "CreatureHitResolutionHook.h"

#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Combat/Hooks/CombatHealthMutationHook.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr std::size_t kMaximumHealthOffset = 0xCC;
    constexpr std::size_t kHealthOffset = 0xD0;
    constexpr std::size_t kThingUidOffset = 0x14;
    constexpr std::size_t kPositionFlagOffset = 0x00;
    constexpr std::size_t kDirectionFlagOffset = 0x01;
    constexpr std::size_t kPositionOffset = 0x02;
    constexpr std::size_t kDirectionOffset = 0x0E;
    constexpr std::size_t kSourcePointerOffset = 0x58;
    constexpr std::size_t kIntelligentPointerHeaderOffset = sizeof(void*);
    constexpr std::size_t kActiveActionOffset = 0x120;

    bool ReadByte(const std::uint8_t* object, std::size_t offset,
        std::uint8_t& value) noexcept
    {
        __try
        {
            value = *(object + offset);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            value = 0;
            return false;
        }
    }

    bool ReadFloat(const std::uint8_t* object, std::size_t offset,
        float& value) noexcept
    {
        __try
        {
            std::memcpy(&value, object + offset, sizeof(value));
            return std::isfinite(value);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            value = 0.0f;
            return false;
        }
    }

    bool ReadPointer(const std::uint8_t* object, std::size_t offset,
        void*& value) noexcept
    {
        __try
        {
            std::memcpy(&value, object + offset, sizeof(value));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            value = nullptr;
            return false;
        }
    }

    bool ReadVector(const std::uint8_t* object, std::size_t offset,
        fable::game::creature::combat::ResolvedHitEvent::Vector& value) noexcept
    {
        return ReadFloat(object, offset, value.x) &&
            ReadFloat(object, offset + sizeof(float), value.y) &&
            ReadFloat(object, offset + sizeof(float) * 2, value.z);
    }

    bool RestoreHealth(
        void* creature,
        float currentHealth,
        float maximumHealth) noexcept
    {
        if (creature == nullptr || !std::isfinite(currentHealth) ||
            !std::isfinite(maximumHealth) || maximumHealth <= 0.0f ||
            currentHealth < 0.0f || currentHealth > maximumHealth + 0.01f)
        {
            return false;
        }
        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(creature);
            *reinterpret_cast<float*>(bytes + kMaximumHealthOffset) =
                maximumHealth;
            *reinterpret_cast<float*>(bytes + kHealthOffset) = currentHealth;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void* ReadActiveAction(void* creature) noexcept
    {
        void* action = nullptr;
        __try
        {
            if (creature != nullptr)
            {
                action = *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(creature) +
                    kActiveActionOffset);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            action = nullptr;
        }
        return action;
    }
}

namespace fable::game::creature::combat
{
    CreatureHitResolutionHook* CreatureHitResolutionHook::active_ = nullptr;

    bool CreatureHitResolutionHook::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;
#if !defined(_M_IX86)
        diagnostics_.Log(
            "Hook: creature hit resolution observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            return false;
        }
        std::uint8_t* target = nullptr;
        if (!native::CreatureHitResolutionFunction::Resolve(gameModule, target))
        {
            diagnostics_.Log(
                "Hook: current CThingCreatureBase OnHit signature validation failed.");
            return false;
        }
        if (!hook_.Install(
                target,
                native::CreatureHitResolutionFunction::ExpectedPrefix.data(),
                native::CreatureHitResolutionFunction::ExpectedPrefix.size(),
                reinterpret_cast<void*>(&CreatureHitResolutionHook::Intercept),
                native::CreatureHitResolutionFunction::DisplacedBytes))
        {
            return false;
        }
        gameModule_ = gameModule;
        original_ = reinterpret_cast<
            native::CreatureHitResolutionFunction::Pointer>(hook_.Original());
        active_ = this;

        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "target=%p replacement=%p trampoline=%p function_rva=0x%08X",
            target,
            &CreatureHitResolutionHook::Intercept,
            hook_.Original(),
            static_cast<unsigned int>(
                native::CreatureHitResolutionFunction::AddressRva));
        diagnostics_.Event("CreatureHitResolutionHookReady", detail);
        diagnostics_.Log(
            "Hook: current CThingCreatureBase OnHit observation boundary installed.");
        return true;
#endif
    }

    void CreatureHitResolutionHook::Shutdown() noexcept
    {
        if (hook_.IsInstalled() && !hook_.Shutdown())
        {
            diagnostics_.Log(
                "Hook: creature hit-resolution shutdown skipped because its target changed.");
            return;
        }
        if (hook_.ProtectionRestoreFailed())
        {
            diagnostics_.Log(
                "Hook: creature hit-resolution bytes restored, but code protection restoration failed.");
        }
        SetEventSink(nullptr, nullptr);
        if (active_ == this) active_ = nullptr;
        original_ = nullptr;
        gameModule_ = nullptr;
        diagnostics_ = {};
    }

    void CreatureHitResolutionHook::SetEventSink(
        EventSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            eventSink_.store(nullptr, std::memory_order_release);
            eventSinkContext_.store(nullptr, std::memory_order_release);
            return;
        }
        eventSinkContext_.store(context, std::memory_order_release);
        eventSink_.store(sink, std::memory_order_release);
    }

    bool CreatureHitResolutionHook::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr &&
            hook_.IsInstalled() && gameModule_ != nullptr;
    }

    std::uint64_t CreatureHitResolutionHook::ReadThingUid(void* thing) noexcept
    {
        if (thing == nullptr)
        {
            return 0;
        }
        std::uint64_t uid = 0;
        __try
        {
            uid = *reinterpret_cast<const std::uint64_t*>(
                static_cast<const std::uint8_t*>(thing) + kThingUidOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            uid = 0;
        }
        return uid;
    }

    bool CreatureHitResolutionHook::ReadHealth(
        void* creature,
        float& currentHealth,
        float& maximumHealth) noexcept
    {
        currentHealth = -1.0f;
        maximumHealth = -1.0f;
        if (creature == nullptr)
        {
            return false;
        }
        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(creature);
            currentHealth = *reinterpret_cast<const float*>(bytes + kHealthOffset);
            maximumHealth = *reinterpret_cast<const float*>(
                bytes + kMaximumHealthOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            currentHealth = -1.0f;
            maximumHealth = -1.0f;
        }
        return std::isfinite(currentHealth) && std::isfinite(maximumHealth) &&
            maximumHealth > 0.0f && currentHealth >= 0.0f &&
            currentHealth <= maximumHealth + 0.01f;
    }

    bool CreatureHitResolutionHook::ReadHitParameters(
        void* hitParameters,
        ResolvedHitEvent& event,
        void*& sourceThing) noexcept
    {
        sourceThing = nullptr;
        if (hitParameters == nullptr)
        {
            return false;
        }
        const auto* const bytes = static_cast<const std::uint8_t*>(hitParameters);
        std::uint8_t positionFlag = 0;
        std::uint8_t directionFlag = 0;
        if (!ReadByte(bytes, kPositionFlagOffset, positionFlag) ||
            !ReadByte(bytes, kDirectionFlagOffset, directionFlag))
        {
            return false;
        }
        event.positionFlag = positionFlag;
        event.directionFlag = directionFlag;
        if (positionFlag != 0 &&
            !ReadVector(bytes, kPositionOffset, event.position))
        {
            return false;
        }
        if (directionFlag != 0 &&
            !ReadVector(bytes, kDirectionOffset, event.direction))
        {
            return false;
        }

        const std::size_t flags[] = {
            0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21,
            0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
        };
        std::uint8_t values[sizeof(flags)] = {};
        for (std::size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i)
        {
            if (!ReadByte(bytes, flags[i], values[i]))
            {
                return false;
            }
        }
        event.knockDown = values[0] != 0;
        event.decapitate = values[1] != 0;
        event.blockable = values[2] != 0;
        event.flourish = values[3] != 0;
        event.epicSpell = values[4] != 0;
        event.blockCounter = values[5] != 0;
        event.playHitResponse = values[6] != 0;
        event.playHitResponseOverrideSet = values[7] != 0;
        event.moveBack = values[8] != 0;
        event.createParticleEffectOnHit = values[9] != 0;
        event.createDustParticleEffectOnHit = values[10] != 0;
        event.guaranteeHit = values[11] != 0;
        event.blocked = values[12] != 0;
        event.hitNegated = values[13] != 0;
        event.causeRecoil = values[14] != 0;

        // CIntelligentPointer<CThing> stores its control/header pointer in
        // the second word. The header's first word is the actual CThing.
        // Reading +0x58 directly yields the pointer object's first word,
        // not the source thing.
        void* sourceHeader = nullptr;
        if (!ReadPointer(
                bytes,
                kSourcePointerOffset + kIntelligentPointerHeaderOffset,
                sourceHeader))
        {
            return false;
        }
        void* source = nullptr;
        if (sourceHeader != nullptr &&
            !ReadPointer(
                static_cast<const std::uint8_t*>(sourceHeader),
                0,
                source))
        {
            return false;
        }
        sourceThing = source;
        event.sourceThingUid = ReadThingUid(source);
        return true;
    }

    void __fastcall CreatureHitResolutionHook::Intercept(
        void* creature,
        void*,
        void* hitParameters)
    {
        CreatureHitResolutionHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            return;
        }
        ResolvedHitEvent event;
        event.targetThingUid = ReadThingUid(creature);
        event.observedAt = GetTickCount64();
        event.threadId = GetCurrentThreadId();
        void* sourceThing = nullptr;
        ReadHitParameters(hitParameters, event, sourceThing);
        if (sourceThing != nullptr && actions::CreatureActionLifecycleObserver::
                IsActiveActionAuthoritativeReplay(sourceThing))
        {
            const unsigned int ordinal =
                hook->suppressedReplayHitCount_.fetch_add(
                    1, std::memory_order_acq_rel) + 1;
            if (ordinal <= 32)
            {
                char detail[256] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "ordinal=%u target_uid=%016llX source_uid=%016llX reason=presentation-replay-does-not-resolve-retail-hit",
                    ordinal,
                    static_cast<unsigned long long>(event.targetThingUid),
                    static_cast<unsigned long long>(event.sourceThingUid));
                hook->diagnostics_.Event(
                    "CombatReplayHitResolutionSuppressed", detail);
            }
            return;
        }
        event.hasPreviousHealth = ReadHealth(
            creature, event.previousHealth, event.maximumHealth);
        const float previousMaximum = event.maximumHealth;
        void* const previousAction = ReadActiveAction(creature);
        CombatHealthMutationHook::ClearProtectedReplicaAttempt();
        hook->original_(creature, hitParameters);
        void* const reactionAction = ReadActiveAction(creature);
        if (reactionAction != nullptr && reactionAction != previousAction &&
            actions::CreatureActionLifecycleObserver::DescribeActionType(
                reactionAction,
                event.reactionActionType,
                sizeof(event.reactionActionType)))
        {
            event.reactionAnimationId = actions::
                CreatureActionLifecycleObserver::
                    DescribeActionAnimationId(reactionAction);
        }
        float postMaximum = -1.0f;
        event.hasCurrentHealth = ReadHealth(
            creature, event.currentHealth, postMaximum);
        float attemptedCurrent = -1.0f;
        float attemptedMaximum = -1.0f;
        const bool protectedAttemptObserved =
            CombatHealthMutationHook::ConsumeProtectedReplicaAttempt(
                creature, attemptedCurrent, attemptedMaximum);
        if (protectedAttemptObserved)
        {
            event.currentHealth = attemptedCurrent;
            postMaximum = attemptedMaximum;
            event.hasCurrentHealth = true;
        }
        if (event.hasCurrentHealth)
        {
            event.maximumHealth = postMaximum;
        }
        if (event.hasPreviousHealth && event.hasCurrentHealth &&
            CombatHealthMutationHook::IsProtectedReplica(creature) &&
            (std::fabs(event.currentHealth - event.previousHealth) >= 0.0001f ||
                std::fabs(postMaximum - previousMaximum) >= 0.0001f))
        {
            const float attemptedHealth = event.currentHealth;
            if (RestoreHealth(
                    creature, event.previousHealth, previousMaximum))
            {
                if (!protectedAttemptObserved)
                {
                    event.currentHealth = event.previousHealth;
                    event.maximumHealth = previousMaximum;
                }
                const unsigned int rejected =
                    hook->rejectedReplicaHitCount_.fetch_add(
                        1, std::memory_order_acq_rel) + 1;
                if (rejected <= 16)
                {
                    char detail[256] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "ordinal=%u target_uid=%016llX retained=%.3f attempted=%.3f authority=remote-owner",
                        rejected,
                        static_cast<unsigned long long>(event.targetThingUid),
                        event.previousHealth,
                        attemptedHealth);
                    hook->diagnostics_.Event(
                        "CombatReplicaHitHealthRestored", detail);
                }
            }
        }
        const EventSink sink = hook->eventSink_.load(std::memory_order_acquire);
        if (sink != nullptr)
        {
            sink(
                hook->eventSinkContext_.load(std::memory_order_acquire),
                event);
        }
        const unsigned int ordinal = hook->observedCount_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        if (ordinal <= 32)
        {
            char detail[512] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "ordinal=%u target_uid=%016llX source_uid=%016llX previous=%.3f current=%.3f maximum=%.3f position=%u direction=%u reaction_action=%s reaction_animation_id=%u",
                ordinal,
                static_cast<unsigned long long>(event.targetThingUid),
                static_cast<unsigned long long>(event.sourceThingUid),
                event.previousHealth,
                event.currentHealth,
                event.maximumHealth,
                event.positionFlag,
                event.directionFlag,
                event.reactionActionType[0] != '\0'
                    ? event.reactionActionType
                    : "<none>",
                event.reactionAnimationId);
            hook->diagnostics_.Event("CreatureHitResolved", detail);
        }
    }
}
