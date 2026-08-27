#include "RemoteHeroDefinitionHook.h"

#include "Game/Entity/Native/ThingComponentProvisioner.h"
#include "Game/Native/Addresses.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr std::size_t kHookDisplacedBytes = 7;

    constexpr std::array<const char*, 7> kHeroComponents = {
        "CTCInventory",
        "CTCHeroMorph",
        "CTCSkeletalMorph",
        "CTCHeroAttachableAppearanceModifiers",
        "CTCWound",
        "CTCTextureDecal",
        "CTCInventoryClothing",
    };

    bool MatchesPrefix(
        const std::uint8_t* address,
        const std::uint8_t* expected,
        std::size_t size) noexcept
    {
        __try
        {
            return address != nullptr &&
                std::memcmp(address, expected, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace fable::game::hero_pawn::appearance::hooks
{
    static_assert(sizeof(void*) == 4);

    RemoteHeroDefinitionHook* RemoteHeroDefinitionHook::active_ = nullptr;

    bool RemoteHeroDefinitionHook::Install(
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

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const lookupTarget = reinterpret_cast<std::uint8_t*>(
            base + game::native::rva::DefinitionByIndex);
        auto* const applyTarget = reinterpret_cast<std::uint8_t*>(
            base + game::native::rva::ThingCreatureApplyDefinition);
        constexpr std::uint8_t kExpectedPrefix[] = {0x6A, 0xFF, 0x68};
        if (!MatchesPrefix(
                lookupTarget, kExpectedPrefix, sizeof(kExpectedPrefix)) ||
            !MatchesPrefix(
                applyTarget, kExpectedPrefix, sizeof(kExpectedPrefix)))
        {
            diagnostics_.Log(
                "Hook: remote Hero runtime-definition ABI validation failed.");
            return false;
        }
        if (!lookupPatch_.Install(
                lookupTarget,
                kExpectedPrefix,
                sizeof(kExpectedPrefix),
                reinterpret_cast<void*>(&RemoteHeroDefinitionHook::LookupDefinition),
                kHookDisplacedBytes))
        {
            return false;
        }
        originalLookup_ = reinterpret_cast<DefinitionLookup>(
            lookupPatch_.Original());
        if (!applyPatch_.Install(
                applyTarget,
                kExpectedPrefix,
                sizeof(kExpectedPrefix),
                reinterpret_cast<void*>(
                    &RemoteHeroDefinitionHook::ApplyRemoteDefinition),
                kHookDisplacedBytes))
        {
            (void)lookupPatch_.Shutdown();
            originalLookup_ = nullptr;
            return false;
        }
        originalApply_ = reinterpret_cast<ApplyDefinition>(
            applyPatch_.Original());
        gameModule_ = gameModule;
        active_ = this;
        diagnostics_.Log(
            "Hook: actor-scoped remote Hero runtime definition installed.");
        diagnostics_.Event(
            "MultiplayerRemoteHeroRuntimeDefinitionReady",
            "retail definitions will be cloned in memory for remote Hero construction; no compiled-definition sidecar is used");
        return true;
    }

    void RemoteHeroDefinitionHook::Shutdown() noexcept
    {
        Cancel();
        if (applyPatch_.IsInstalled() && !applyPatch_.Shutdown())
        {
            diagnostics_.Log(
                "Hook: remote Hero definition component patch remains owned by another hook.");
            return;
        }
        if (lookupPatch_.IsInstalled() && !lookupPatch_.Shutdown())
        {
            diagnostics_.Log(
                "Hook: remote Hero definition lookup patch remains owned by another hook.");
            return;
        }
        if (active_ == this)
        {
            active_ = nullptr;
        }
        runtimeDefinition_.AbandonForProcessLifetime();
        originalLookup_ = nullptr;
        originalApply_ = nullptr;
        gameModule_ = nullptr;
        armedThread_.store(0, std::memory_order_relaxed);
        armToken_.store(0, std::memory_order_relaxed);
        substitutions_.store(0, std::memory_order_relaxed);
        diagnostics_ = {};
    }

    RemoteHeroDefinitionHook::ArmToken RemoteHeroDefinitionHook::Arm() noexcept
    {
        if (!IsInstalled() ||
            !runtimeDefinition_.Ensure(
                gameModule_, originalLookup_, diagnostics_))
        {
            return 0;
        }
        const ArmToken token = armToken_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        substitutions_.store(0, std::memory_order_relaxed);
        armedThread_.store(GetCurrentThreadId(), std::memory_order_relaxed);
        armed_.store(true, std::memory_order_release);
        return token;
    }

    void RemoteHeroDefinitionHook::Cancel(ArmToken token) noexcept
    {
        if (token != 0 &&
            token != armToken_.load(std::memory_order_acquire))
        {
            return;
        }
        armed_.store(false, std::memory_order_release);
        armedThread_.store(0, std::memory_order_relaxed);
    }

    bool RemoteHeroDefinitionHook::IsInstalled() const noexcept
    {
        return active_ == this && gameModule_ != nullptr &&
            originalLookup_ != nullptr && originalApply_ != nullptr &&
            lookupPatch_.IsInstalled() && applyPatch_.IsInstalled();
    }

    bool RemoteHeroDefinitionHook::IsArmed() const noexcept
    {
        return armed_.load(std::memory_order_acquire);
    }

    bool RemoteHeroDefinitionHook::IsActiveOnCurrentThread() const noexcept
    {
        return armed_.load(std::memory_order_acquire) &&
            armedThread_.load(std::memory_order_relaxed) ==
                GetCurrentThreadId();
    }

    bool RemoteHeroDefinitionHook::ProvisionHeroComponents(
        void* thing) noexcept
    {
        if (thing == nullptr)
        {
            return false;
        }
        for (const char* const component : kHeroComponents)
        {
            if (game::entity::native::ThingComponentProvisioner::AddNamed(
                    gameModule_, thing, component) == nullptr)
            {
                char detail[160] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "thing=%p component=%s",
                    thing,
                    component);
                diagnostics_.Event(
                    "MultiplayerRemoteHeroComponentFailed",
                    detail);
                return false;
            }
        }
        diagnostics_.Event(
            "MultiplayerRemoteHeroComponentsProvisioned",
            "actor received the retail Hero presentation component set from its private runtime definition");
        return true;
    }

    bool __fastcall RemoteHeroDefinitionHook::LookupDefinition(
        void* definitionManager,
        void*,
        unsigned int definitionIndex,
        void** result)
    {
        RemoteHeroDefinitionHook* const hook = active_;
        if (hook == nullptr || hook->originalLookup_ == nullptr)
        {
            return false;
        }
        const bool found = hook->originalLookup_(
            definitionManager, definitionIndex, result);
        if (found &&
            definitionIndex ==
                hook->runtimeDefinition_.RetailBaseIndex() &&
            hook->IsActiveOnCurrentThread() &&
            hook->runtimeDefinition_.ReplaceReference(result))
        {
            hook->substitutions_.fetch_add(1, std::memory_order_relaxed);
        }
        return found;
    }

    void __fastcall RemoteHeroDefinitionHook::ApplyRemoteDefinition(
        void* thing,
        void*,
        void* definition)
    {
        RemoteHeroDefinitionHook* const hook = active_;
        if (hook == nullptr || hook->originalApply_ == nullptr)
        {
            return;
        }
        hook->originalApply_(thing, definition);
        if (definition == hook->runtimeDefinition_.Get() &&
            hook->IsActiveOnCurrentThread())
        {
            (void)hook->ProvisionHeroComponents(thing);
        }
    }
}
