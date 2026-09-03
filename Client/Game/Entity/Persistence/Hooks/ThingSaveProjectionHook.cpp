#include "ThingSaveProjectionHook.h"

#include "Game/NPC/Simulation/DummyVillager/Native/DummyVillagerFunctions.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    struct ThingIdentity final
    {
        std::uint64_t uid = 0;
        std::uint64_t simulationCreatureUid = 0;
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
            fable::game::npc::simulation::DummyVillagerState lowSimulation;
            if (fable::game::npc::simulation::native::
                    DummyVillagerFunctions::Read(thing, lowSimulation) &&
                lowSimulation.componentPresent)
            {
                identity.simulationCreatureUid = lowSimulation.creatureUid;
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
        if (active_ == this)
        {
            diagnostics_.Log(
                "Hook: CThing save installation is partially active; shutdown is required before retrying.");
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
                saveDetour_.Original());

        if (!InstallDetour(
                loadTarget,
                reinterpret_cast<void*>(
                    &ThingSaveProjectionHook::LoadProjected),
                loadDetour_))
        {
            const bool rollbackRestored = RestoreDetour(saveDetour_);
            if (!rollbackRestored)
            {
                diagnostics_.Log(
                    "Hook: CThing save rollback deferred because a target is owned by another hook.");
                return false;
            }
            originalSave_ = nullptr;
            active_ = nullptr;
            return false;
        }
        originalLoad_ =
            reinterpret_cast<native::ThingSaveFunctions::LoadPointer>(
                loadDetour_.Original());

        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "save=%p save_trampoline=%p load=%p load_trampoline=%p map_offset=0x9A uid_offset=0x14 definition_offset=0x98 script_offset=0x80",
            saveTarget,
            saveDetour_.Original(),
            loadTarget,
            loadDetour_.Original());
        diagnostics_.Event("ThingPersistenceProjectionReady", detail);
        return true;
#endif
    }

    void ThingSaveProjectionHook::Shutdown() noexcept
    {
        bool allRestored = true;
        allRestored = RestoreDetour(loadDetour_) && allRestored;
        allRestored = RestoreDetour(saveDetour_) && allRestored;
        if (!allRestored)
        {
            diagnostics_.Log(
                "Hook: Thing persistence shutdown deferred because a target is owned by another hook.");
            return;
        }
        SetMapOverrideSink(nullptr, nullptr);
        if (active_ == this) active_ = nullptr;
        originalLoad_ = nullptr;
        originalSave_ = nullptr;
        diagnostics_ = {};
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
        return active_ == this && saveDetour_.IsInstalled() &&
            loadDetour_.IsInstalled() &&
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

        // Remote Heroes are process-local replicated presentations. Their
        // native persistent bit prevents retail distance culling, but must not
        // make them part of either player's durable save. The true local Hero
        // keeps SCRIPT_NAME_HERO and continues through the normal save path.
        if (std::strcmp(
                identity.scriptName.data(),
                "SCRIPT_NAME_ALBION_TOGETHER_REMOTE_PLAYER") == 0)
        {
            hook->diagnostics_.Event(
                "ThingPersistenceSaveFiltered",
                "skipped process-local remote Hero presentation");
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
                    identity.simulationCreatureUid,
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
                    identity.simulationCreatureUid,
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
        core::hooking::InlineHook& detour) noexcept
    {
        constexpr std::size_t displacedBytes =
            native::ThingSaveFunctions::DisplacedBytes;
        if (target == nullptr || replacement == nullptr || detour.IsInstalled())
        {
            return false;
        }
        return detour.Install(
            target,
            target,
            displacedBytes,
            replacement,
            displacedBytes);
    }

    bool ThingSaveProjectionHook::RestoreDetour(
        core::hooking::InlineHook& detour) noexcept
    {
        return detour.Shutdown();
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
