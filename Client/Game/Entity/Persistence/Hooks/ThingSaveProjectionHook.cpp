#include "ThingSaveProjectionHook.h"

#include <array>
#include <climits>
#include <cstdio>
#include <cstring>

namespace
{
    struct ThingIdentity final
    {
        std::uint64_t uid = 0;
        std::uint16_t definitionIndex = 0;
        std::uint16_t mapId = 0;
        std::array<char, 96> scriptName = {};
    };

    bool ReadThingIdentity(void* thing, ThingIdentity& identity) noexcept
    {
        identity = {};
        if (thing == nullptr)
        {
            return false;
        }

        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(thing);
            identity.uid = *reinterpret_cast<const std::uint64_t*>(
                bytes + 0x14);
            identity.definitionIndex =
                *reinterpret_cast<const std::uint16_t*>(bytes + 0x98);
            identity.mapId = *reinterpret_cast<const std::uint16_t*>(
                bytes + 0x9A);

            void* const scriptString =
                *reinterpret_cast<void* const*>(bytes + 0x80);
            if (scriptString != nullptr)
            {
                const char* const text =
                    *reinterpret_cast<const char* const*>(
                        static_cast<const std::uint8_t*>(scriptString) +
                        sizeof(void*));
                if (text != nullptr)
                {
                    std::size_t index = 0;
                    while (index + 1 < identity.scriptName.size() &&
                        text[index] != '\0')
                    {
                        identity.scriptName[index] = text[index];
                        ++index;
                    }
                    identity.scriptName[index] = '\0';
                }
            }
            return identity.uid != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            identity = {};
            return false;
        }
    }
}

namespace fable::game::entity::persistence
{
    ThingSaveProjectionHook* ThingSaveProjectionHook::active_ = nullptr;

    bool ThingSaveProjectionHook::Install(
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
            "Hook: Thing save/load projection is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            return false;
        }

        std::uint8_t* saveTarget = nullptr;
        std::uint8_t* loadTarget = nullptr;
        if (!native::ThingSaveFunctions::ResolveSave(gameModule, saveTarget) ||
            !native::ThingSaveFunctions::ResolveLoad(gameModule, loadTarget))
        {
            diagnostics_.Log(
                "Hook: CThing save/load projection definitions failed validation.");
            return false;
        }

        active_ = this;
        if (!InstallDetour(
                saveTarget,
                reinterpret_cast<void*>(
                    &ThingSaveProjectionHook::SaveProjected),
                saveDetour_))
        {
            active_ = nullptr;
            return false;
        }
        originalSave_ =
            reinterpret_cast<native::ThingSaveFunctions::SavePointer>(
                saveDetour_.trampoline);

        if (!InstallDetour(
                loadTarget,
                reinterpret_cast<void*>(
                    &ThingSaveProjectionHook::LoadProjected),
                loadDetour_))
        {
            RestoreDetour(saveDetour_);
            originalSave_ = nullptr;
            active_ = nullptr;
            return false;
        }
        originalLoad_ =
            reinterpret_cast<native::ThingSaveFunctions::LoadPointer>(
                loadDetour_.trampoline);

        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "save=%p save_trampoline=%p load=%p load_trampoline=%p map_offset=0x9A uid_offset=0x14 definition_offset=0x98 script_offset=0x80",
            saveDetour_.target,
            saveDetour_.trampoline,
            loadDetour_.target,
            loadDetour_.trampoline);
        diagnostics_.Event("ThingPersistenceProjectionReady", detail);
        return true;
#endif
    }

    void ThingSaveProjectionHook::SetMapOverrideSink(
        MapOverrideSink sink,
        void* context) noexcept
    {
        sinkContext_.store(context, std::memory_order_release);
        sink_.store(sink, std::memory_order_release);
    }

    bool ThingSaveProjectionHook::IsInstalled() const noexcept
    {
        return active_ == this && saveDetour_.target != nullptr &&
            saveDetour_.trampoline != nullptr &&
            loadDetour_.target != nullptr &&
            loadDetour_.trampoline != nullptr &&
            originalSave_ != nullptr && originalLoad_ != nullptr;
    }

    void __fastcall ThingSaveProjectionHook::SaveProjected(
        void* thing,
        void*,
        void* writer)
    {
        ThingSaveProjectionHook* const hook = active_;
        if (hook == nullptr || hook->originalSave_ == nullptr || thing == nullptr)
        {
            return;
        }

        ThingIdentity identity;
        if (!ReadThingIdentity(thing, identity))
        {
            hook->originalSave_(thing, writer);
            return;
        }

        const std::uint16_t originalMapId = identity.mapId;
        std::uint16_t projectedMapId = originalMapId;
        bool projected = false;
        __try
        {
            const MapOverrideSink sink = hook->sink_.load(
                std::memory_order_acquire);
            if (sink != nullptr &&
                sink(
                    hook->sinkContext_.load(std::memory_order_acquire),
                    identity.uid,
                    identity.definitionIndex,
                    identity.scriptName.data(),
                    projectedMapId) &&
                projectedMapId != 0 && projectedMapId != originalMapId)
            {
                *reinterpret_cast<std::uint16_t*>(
                    static_cast<std::uint8_t*>(thing) + 0x9A) =
                        projectedMapId;
                projected = true;
            }

            hook->originalSave_(thing, writer);
        }
        __finally
        {
            if (projected)
            {
                *reinterpret_cast<std::uint16_t*>(
                    static_cast<std::uint8_t*>(thing) + 0x9A) =
                        originalMapId;
            }
        }
        if (projected)
        {
            hook->ReportProjection(
                "save",
                identity.uid,
                originalMapId,
                projectedMapId);
        }
    }

    bool __fastcall ThingSaveProjectionHook::LoadProjected(
        void* thing,
        void*,
        void* reader)
    {
        ThingSaveProjectionHook* const hook = active_;
        if (hook == nullptr || hook->originalLoad_ == nullptr || thing == nullptr)
        {
            return false;
        }

        const bool loaded = hook->originalLoad_(thing, reader);
        if (!loaded)
        {
            return false;
        }

        ThingIdentity identity;
        if (!ReadThingIdentity(thing, identity))
        {
            return true;
        }

        std::uint16_t projectedMapId = identity.mapId;
        bool projected = false;
        __try
        {
            const MapOverrideSink sink = hook->sink_.load(
                std::memory_order_acquire);
            if (sink != nullptr &&
                sink(
                    hook->sinkContext_.load(std::memory_order_acquire),
                    identity.uid,
                    identity.definitionIndex,
                    identity.scriptName.data(),
                    projectedMapId) &&
                projectedMapId != 0 && projectedMapId != identity.mapId)
            {
                *reinterpret_cast<std::uint16_t*>(
                    static_cast<std::uint8_t*>(thing) + 0x9A) =
                        projectedMapId;
                projected = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            projected = false;
        }

        if (projected)
        {
            hook->ReportProjection(
                "load",
                identity.uid,
                identity.mapId,
                projectedMapId);
        }
        return true;
    }

    bool ThingSaveProjectionHook::InstallDetour(
        std::uint8_t* target,
        void* replacement,
        Detour& detour) noexcept
    {
        constexpr std::size_t displacedBytes =
            native::ThingSaveFunctions::DisplacedBytes;
        if (target == nullptr || replacement == nullptr ||
            detour.target != nullptr)
        {
            return false;
        }
        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            displacedBytes + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (trampoline == nullptr)
        {
            return false;
        }

        std::memcpy(detour.originalBytes.data(), target, displacedBytes);
        std::memcpy(trampoline, target, displacedBytes);
        const std::intptr_t trampolineDisplacement =
            reinterpret_cast<std::intptr_t>(target + displacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + displacedBytes) + 5);
        const std::intptr_t replacementDisplacement =
            reinterpret_cast<std::intptr_t>(replacement) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (trampolineDisplacement < INT32_MIN ||
            trampolineDisplacement > INT32_MAX ||
            replacementDisplacement < INT32_MIN ||
            replacementDisplacement > INT32_MAX)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        trampoline[displacedBytes] = 0xE9;
        const std::int32_t trampolineRelative =
            static_cast<std::int32_t>(trampolineDisplacement);
        std::memcpy(
            trampoline + displacedBytes + 1,
            &trampolineRelative,
            sizeof(trampolineRelative));

        std::array<std::uint8_t, displacedBytes> patch = {};
        patch.fill(0x90);
        patch[0] = 0xE9;
        const std::int32_t replacementRelative =
            static_cast<std::int32_t>(replacementDisplacement);
        std::memcpy(
            patch.data() + 1,
            &replacementRelative,
            sizeof(replacementRelative));

        DWORD previousProtection = 0;
        if (!VirtualProtect(
                target,
                patch.size(),
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        detour.target = target;
        detour.trampoline = trampoline;
        std::memcpy(target, patch.data(), patch.size());
        FlushInstructionCache(GetCurrentProcess(), target, patch.size());
        FlushInstructionCache(
            GetCurrentProcess(),
            trampoline,
            displacedBytes + 5);
        DWORD discarded = 0;
        VirtualProtect(target, patch.size(), previousProtection, &discarded);
        return true;
    }

    void ThingSaveProjectionHook::RestoreDetour(Detour& detour) noexcept
    {
        if (detour.target == nullptr)
        {
            return;
        }
        DWORD previousProtection = 0;
        if (VirtualProtect(
                detour.target,
                detour.originalBytes.size(),
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            std::memcpy(
                detour.target,
                detour.originalBytes.data(),
                detour.originalBytes.size());
            FlushInstructionCache(
                GetCurrentProcess(),
                detour.target,
                detour.originalBytes.size());
            DWORD discarded = 0;
            VirtualProtect(
                detour.target,
                detour.originalBytes.size(),
                previousProtection,
                &discarded);
        }
        if (detour.trampoline != nullptr)
        {
            VirtualFree(detour.trampoline, 0, MEM_RELEASE);
        }
        detour = {};
    }

    void ThingSaveProjectionHook::ReportProjection(
        const char* phase,
        std::uint64_t thingUid,
        std::uint16_t fromMapId,
        std::uint16_t toMapId) noexcept
    {
        const unsigned int ordinal = projectionCount_.fetch_add(
            1,
            std::memory_order_acq_rel) + 1;
        if (ordinal > DiagnosticEventLimit)
        {
            return;
        }
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "phase=%s ordinal=%u thing_uid=%016llX native_map_id=%u canonical_map_id=%u",
            phase != nullptr ? phase : "unknown",
            ordinal,
            static_cast<unsigned long long>(thingUid),
            static_cast<unsigned int>(fromMapId),
            static_cast<unsigned int>(toMapId));
        diagnostics_.Event("ThingMapProjected", detail);
    }
}
