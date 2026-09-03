#include "WorldSectionPersistenceHook.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <limits>

namespace
{
    using Section = fable::multiplayer::protocol::WorldSection;

    constexpr std::uintptr_t RegionSaveRva = 0x01BCA6A0;
    constexpr std::uintptr_t RegionLoadRva = 0x01BD95A0;
    constexpr std::uintptr_t FactionSaveRva = 0x01879B50;
    constexpr std::uintptr_t FactionLoadRva = 0x01879C70;
    constexpr std::uintptr_t BorrowedInputInitializeRva = 0x012C8250;
    constexpr std::uintptr_t MemoryOutputStreamVtableRva = 0x02A554C0;
    constexpr std::uintptr_t MemoryInputStreamVtableRva = 0x02A553DC;
    constexpr std::uintptr_t RegionLoadReturnRva = 0x01BA2D3F;
    constexpr std::uintptr_t FactionLoadReturnRva = 0x01BA2D8C;
    constexpr std::uintptr_t RegionSaveExceptionRva = 0x0255ED50;
    constexpr std::uintptr_t RegionLoadExceptionRva = 0x02560519;
    constexpr std::uintptr_t FactionSaveExceptionRva = 0x0251B218;
    constexpr std::uintptr_t FactionLoadExceptionRva = 0x0251B260;
    constexpr std::size_t HookPrefixBytes = 14;
    constexpr std::uint32_t SaveModeBinary = 3;
    constexpr std::uint32_t LoadModeBinary = 2;

    constexpr std::array<std::uint8_t, HookPrefixBytes> PrefixTemplate = {
        0x6A, 0xFF, 0x68, 0, 0, 0, 0,
        0x64, 0xA1, 0, 0, 0, 0, 0x50};
    constexpr std::array<std::uint8_t, 28> BorrowedInputPrefix = {
        0x8B, 0x54, 0x24, 0x08, 0x33, 0xC0, 0x89, 0x41,
        0x04, 0x89, 0x41, 0x10, 0x8B, 0x44, 0x24, 0x04,
        0x89, 0x41, 0x0C, 0x89, 0x51, 0x08, 0x89, 0x51,
        0x14, 0x89, 0x41, 0x18};

    struct MemoryOutputStreamView final
    {
        void* vtable;
        std::uint32_t reserved;
        std::uint8_t* begin;
        std::uint8_t* end;
        std::uint8_t* capacity;
        std::uint32_t reserved2;
        std::uint32_t position;
    };
    static_assert(offsetof(MemoryOutputStreamView, begin) == 0x08);
    static_assert(offsetof(MemoryOutputStreamView, position) == 0x18);

    struct MemoryInputStreamView final
    {
        std::array<std::uint8_t, 0x1C> bytes = {};
    };

    struct CapturedOutput final
    {
        MemoryOutputStreamView* stream = nullptr;
        std::uint32_t start = 0;
        bool valid = false;
    };

    bool ReadCaptureStart(
        void* const context,
        const std::uintptr_t gameBase,
        CapturedOutput& capture) noexcept
    {
#if !defined(_M_IX86)
        (void)context;
        (void)capture;
        return false;
#else
        __try
        {
            const auto address = reinterpret_cast<std::uint8_t*>(context);
            if (address == nullptr ||
                *reinterpret_cast<std::uint32_t*>(address + 0x18) !=
                    SaveModeBinary)
            {
                return false;
            }
            auto* const stream = *reinterpret_cast<MemoryOutputStreamView**>(
                address + 0x28);
            if (stream == nullptr ||
                stream->vtable != reinterpret_cast<void*>(
                    gameBase + MemoryOutputStreamVtableRva) ||
                stream->begin == nullptr || stream->end < stream->begin ||
                stream->capacity < stream->end)
            {
                return false;
            }
            const auto size = static_cast<std::size_t>(
                stream->end - stream->begin);
            if (stream->position > size)
            {
                return false;
            }
            capture.stream = stream;
            capture.start = stream->position;
            capture.valid = true;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            capture = {};
            return false;
        }
#endif
    }

    bool ReadCapturedBytes(
        const CapturedOutput& capture,
        const std::uint8_t*& bytes,
        std::size_t& byteCount) noexcept
    {
        bytes = nullptr;
        byteCount = 0;
#if !defined(_M_IX86)
        (void)capture;
        return false;
#else
        __try
        {
            if (!capture.valid || capture.stream == nullptr ||
                capture.stream->begin == nullptr ||
                capture.stream->end < capture.stream->begin ||
                capture.stream->capacity < capture.stream->end)
            {
                return false;
            }
            const auto size = static_cast<std::size_t>(
                capture.stream->end - capture.stream->begin);
            const std::uint32_t finish = capture.stream->position;
            if (finish < capture.start || finish > size ||
                finish - capture.start >
                    fable::multiplayer::protocol::
                        MaximumWorldSectionSnapshotBytes)
            {
                return false;
            }
            bytes = capture.stream->begin + capture.start;
            byteCount = finish - capture.start;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            bytes = nullptr;
            byteCount = 0;
            return false;
        }
#endif
    }

    bool ValidateGuestContext(
        void* const context,
        const std::uintptr_t gameBase) noexcept
    {
#if !defined(_M_IX86)
        (void)context;
        return false;
#else
        __try
        {
            auto* const address = reinterpret_cast<std::uint8_t*>(context);
            if (address == nullptr ||
                *reinterpret_cast<std::uint32_t*>(address + 0x18) !=
                    LoadModeBinary ||
                *reinterpret_cast<void**>(address + 0x24) != address + 0x30 ||
                *reinterpret_cast<void**>(address + 0x30) !=
                    reinterpret_cast<void*>(
                        gameBase + MemoryInputStreamVtableRva))
            {
                return false;
            }
            const auto* const view = address + 0x30;
            const auto total = *reinterpret_cast<const std::uint32_t*>(
                view + 0x08);
            const auto* const source =
                *reinterpret_cast<const std::uint8_t* const*>(view + 0x0C);
            return *reinterpret_cast<const std::uint32_t*>(view + 0x04) == 0 &&
                total != 0 &&
                total <= fable::multiplayer::protocol::
                    MaximumWorldSectionSnapshotBytes &&
                source != nullptr &&
                *reinterpret_cast<const std::uint32_t*>(view + 0x10) == 0 &&
                *reinterpret_cast<const std::uint32_t*>(view + 0x14) == total &&
                *reinterpret_cast<const std::uint8_t* const*>(view + 0x18) ==
                    source;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
#endif
    }

    bool CopyInputView(
        void* const context,
        MemoryInputStreamView& view) noexcept
    {
#if !defined(_M_IX86)
        (void)context;
        (void)view;
        return false;
#else
        __try
        {
            std::memcpy(view.bytes.data(),
                static_cast<std::uint8_t*>(context) + 0x30,
                view.bytes.size());
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
#endif
    }

    bool ReadInputPayload(
        void* const context,
        const std::uint8_t*& bytes,
        std::size_t& byteCount) noexcept
    {
        bytes = nullptr;
        byteCount = 0;
#if !defined(_M_IX86)
        (void)context;
        return false;
#else
        __try
        {
            const auto* const view =
                static_cast<const std::uint8_t*>(context) + 0x30;
            const auto size = *reinterpret_cast<const std::uint32_t*>(
                view + 0x08);
            const auto* const begin =
                *reinterpret_cast<const std::uint8_t* const*>(view + 0x0C);
            if (size == 0 ||
                size > fable::multiplayer::protocol::
                    MaximumWorldSectionSnapshotBytes ||
                (size != 0 && begin == nullptr))
            {
                return false;
            }
            bytes = begin;
            byteCount = size;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            bytes = nullptr;
            byteCount = 0;
            return false;
        }
#endif
    }

    void RestoreInputView(
        void* const context,
        const MemoryInputStreamView& view) noexcept
    {
#if defined(_M_IX86)
        __try
        {
            std::memcpy(static_cast<std::uint8_t*>(context) + 0x30,
                view.bytes.data(), view.bytes.size());
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
#else
        (void)context;
        (void)view;
#endif
    }

    bool HasExactPrefix(
        const std::uintptr_t address,
        const std::uint8_t* const expected,
        const std::size_t byteCount) noexcept
    {
#if !defined(_M_IX86)
        (void)address; (void)expected; (void)byteCount;
        return false;
#else
        __try
        {
            return address != 0 && expected != nullptr &&
                std::memcmp(reinterpret_cast<const void*>(address),
                    expected, byteCount) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
#endif
    }

    void DescribeRejectedLoadContext(
        const Section section,
        const void* const persistContext,
        char* const detail,
        const std::size_t detailCapacity) noexcept
    {
        if (detail == nullptr || detailCapacity == 0) return;
#if defined(_M_IX86)
        __try
        {
            const auto* const address =
                static_cast<const std::uint8_t*>(persistContext);
            std::snprintf(detail, detailCapacity,
                "section=%s context=%p mode=%u stream=%p inline_vtable=%p "
                "input=(position=%u total=%u begin=%p end_position=%u "
                "capacity=%u cursor=%p)",
                section == Section::Regions ? "regions" : "factions",
                persistContext,
                address == nullptr ? 0u :
                    *reinterpret_cast<const std::uint32_t*>(address + 0x18),
                address == nullptr ? nullptr :
                    *reinterpret_cast<void* const*>(address + 0x24),
                address == nullptr ? nullptr :
                    *reinterpret_cast<void* const*>(address + 0x30),
                address == nullptr ? 0u :
                    *reinterpret_cast<const std::uint32_t*>(address + 0x34),
                address == nullptr ? 0u :
                    *reinterpret_cast<const std::uint32_t*>(address + 0x38),
                address == nullptr ? nullptr :
                    *reinterpret_cast<void* const*>(address + 0x3C),
                address == nullptr ? 0u :
                    *reinterpret_cast<const std::uint32_t*>(address + 0x40),
                address == nullptr ? 0u :
                    *reinterpret_cast<const std::uint32_t*>(address + 0x44),
                address == nullptr ? nullptr :
                    *reinterpret_cast<void* const*>(address + 0x48));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            std::snprintf(detail, detailCapacity,
                "section=%s context=%p unreadable=true",
                section == Section::Regions ? "regions" : "factions",
                persistContext);
        }
#else
        std::snprintf(detail, detailCapacity,
            "section=%s x86_required=true",
            section == Section::Regions ? "regions" : "factions");
#endif
    }
}

namespace fable::game::persistence
{
    WorldSectionPersistenceHook* WorldSectionPersistenceHook::active_ = nullptr;

    bool WorldSectionPersistenceHook::InstallOne(
        core::hooking::InlineHook& hook,
        const std::uintptr_t targetRva,
        const std::uintptr_t exceptionRecordRva,
        void* const replacement,
        PersistFunction& original) noexcept
    {
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        std::array<std::uint8_t, HookPrefixBytes> expected = PrefixTemplate;
        const auto exceptionRecord = static_cast<std::uint32_t>(
            base + exceptionRecordRva);
        std::memcpy(expected.data() + 3, &exceptionRecord,
            sizeof(exceptionRecord));
        if (!hook.Install(reinterpret_cast<void*>(base + targetRva),
                expected.data(), expected.size(), replacement,
                expected.size()))
        {
            return false;
        }
        original = reinterpret_cast<PersistFunction>(hook.Original());
        return original != nullptr;
    }

    bool WorldSectionPersistenceHook::InstallHostCapture(
        const HMODULE gameModule,
        const CaptureSink sink,
        void* const context,
        const core::Diagnostics& diagnostics) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule; (void)sink; (void)context;
        diagnostics.Log("Hook: world-section capture requires the x86 client.");
        return false;
#else
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        if (gameModule == nullptr || sink == nullptr || active_ != nullptr ||
            !HasExactPrefix(base + BorrowedInputInitializeRva,
                BorrowedInputPrefix.data(), BorrowedInputPrefix.size()))
        {
            return false;
        }
        gameModule_ = gameModule;
        diagnostics_ = diagnostics;
        captureSink_ = sink;
        captureContext_ = context;
        active_ = this;
        if (!InstallOne(saveRegionsHook_, RegionSaveRva,
                RegionSaveExceptionRva,
                reinterpret_cast<void*>(&SaveRegionsObserved),
                saveRegionsOriginal_) ||
            !InstallOne(saveFactionsHook_, FactionSaveRva,
                FactionSaveExceptionRva,
                reinterpret_cast<void*>(&SaveFactionsObserved),
                saveFactionsOriginal_) ||
            !InstallOne(loadRegionsHook_, RegionLoadRva,
                RegionLoadExceptionRva,
                reinterpret_cast<void*>(&LoadRegionsObserved),
                loadRegionsOriginal_) ||
            !InstallOne(loadFactionsHook_, FactionLoadRva,
                FactionLoadExceptionRva,
                reinterpret_cast<void*>(&LoadFactionsObserved),
                loadFactionsOriginal_))
        {
            Shutdown();
            return false;
        }
        diagnostics_.Event("MultiplayerWorldSectionCaptureReady",
            "native REGIONS and FACTIONS load/save payload boundaries are observed");
        return true;
#endif
    }

    bool WorldSectionPersistenceHook::InstallGuestOverride(
        const HMODULE gameModule,
        const SnapshotProvider provider,
        const ApplyResultSink resultSink,
        void* const context,
        const core::Diagnostics& diagnostics) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule; (void)provider; (void)resultSink; (void)context;
        diagnostics.Log("Hook: world-section override requires the x86 client.");
        return false;
#else
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        if (gameModule == nullptr || provider == nullptr || active_ != nullptr ||
            !HasExactPrefix(base + BorrowedInputInitializeRva,
                BorrowedInputPrefix.data(), BorrowedInputPrefix.size()))
        {
            return false;
        }
        gameModule_ = gameModule;
        diagnostics_ = diagnostics;
        snapshotProvider_ = provider;
        applyResultSink_ = resultSink;
        loadContext_ = context;
        active_ = this;
        if (!InstallOne(loadRegionsHook_, RegionLoadRva,
                RegionLoadExceptionRva,
                reinterpret_cast<void*>(&LoadRegionsObserved),
                loadRegionsOriginal_) ||
            !InstallOne(loadFactionsHook_, FactionLoadRva,
                FactionLoadExceptionRva,
                reinterpret_cast<void*>(&LoadFactionsObserved),
                loadFactionsOriginal_))
        {
            Shutdown();
            return false;
        }
        diagnostics_.Event("MultiplayerWorldSectionOverrideReady",
            "guest REGIONS and FACTIONS child streams accept authoritative payloads");
        return true;
#endif
    }

    void WorldSectionPersistenceHook::Shutdown() noexcept
    {
        const bool saveRegionsStopped = saveRegionsHook_.Shutdown();
        const bool saveFactionsStopped = saveFactionsHook_.Shutdown();
        const bool loadRegionsStopped = loadRegionsHook_.Shutdown();
        const bool loadFactionsStopped = loadFactionsHook_.Shutdown();
        const bool stopped = saveRegionsStopped && saveFactionsStopped &&
            loadRegionsStopped && loadFactionsStopped;
        if (!stopped)
        {
            diagnostics_.Log("Hook: world-section shutdown was deferred.");
            return;
        }
        saveRegionsOriginal_ = nullptr;
        saveFactionsOriginal_ = nullptr;
        loadRegionsOriginal_ = nullptr;
        loadFactionsOriginal_ = nullptr;
        captureSink_ = nullptr;
        captureContext_ = nullptr;
        snapshotProvider_ = nullptr;
        applyResultSink_ = nullptr;
        loadContext_ = nullptr;
        invalidLoadContextReports_ = {};
        gameModule_ = nullptr;
        diagnostics_ = {};
        if (active_ == this) active_ = nullptr;
    }

    bool WorldSectionPersistenceHook::IsHostCaptureInstalled() const noexcept
    {
        return saveRegionsHook_.IsInstalled() &&
            saveFactionsHook_.IsInstalled() &&
            loadRegionsHook_.IsInstalled() &&
            loadFactionsHook_.IsInstalled();
    }

    bool WorldSectionPersistenceHook::IsGuestOverrideInstalled() const noexcept
    {
        return loadRegionsHook_.IsInstalled() && loadFactionsHook_.IsInstalled();
    }

    void WorldSectionPersistenceHook::ObserveSave(
        const Section section,
        const PersistFunction original,
        void* const manager,
        void* const persistContext)
    {
        CapturedOutput capture;
        ReadCaptureStart(persistContext,
            reinterpret_cast<std::uintptr_t>(gameModule_), capture);
        original(manager, persistContext);
        const std::uint8_t* bytes = nullptr;
        std::size_t byteCount = 0;
        if (captureSink_ != nullptr &&
            ReadCapturedBytes(capture, bytes, byteCount))
        {
            captureSink_(captureContext_, section, bytes, byteCount);
        }
    }

    void WorldSectionPersistenceHook::ObserveLoad(
        const Section section,
        const PersistFunction original,
        const std::uintptr_t expectedReturnRva,
        void* const manager,
        void* const persistContext,
        const void* const returnAddress)
    {
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        bool applied = false;
        std::shared_ptr<const std::vector<std::uint8_t>> snapshot;
        MemoryInputStreamView originalView;
        const bool matchingDispatcher = base != 0 &&
            reinterpret_cast<std::uintptr_t>(returnAddress) ==
                base + expectedReturnRva;
        const bool exactSectionCall = matchingDispatcher &&
            ValidateGuestContext(persistContext, base);
        if (matchingDispatcher && !exactSectionCall &&
            invalidLoadContextReports_[section == Section::Factions ? 1 : 0]++ == 0)
        {
            char detail[512] = {};
            DescribeRejectedLoadContext(
                section, persistContext, detail, sizeof(detail));
            diagnostics_.Event("MultiplayerWorldSectionContextRejected", detail);
        }
        if (matchingDispatcher && !exactSectionCall &&
            snapshotProvider_ != nullptr)
        {
            if (applyResultSink_ != nullptr)
            {
                applyResultSink_(loadContext_, section, false);
            }
            return;
        }
        if (exactSectionCall && captureSink_ != nullptr)
        {
            const std::uint8_t* bytes = nullptr;
            std::size_t byteCount = 0;
            if (ReadInputPayload(persistContext, bytes, byteCount))
            {
                // Copy synchronously before retail advances the borrowed
                // stream. The authority service retains its own immutable
                // payload; no native pointer crosses this call.
                captureSink_(captureContext_, section, bytes, byteCount);
            }
        }
        if (exactSectionCall &&
            snapshotProvider_ != nullptr &&
            snapshotProvider_(loadContext_, section, snapshot) && snapshot &&
            snapshot->size() <= multiplayer::protocol::
                MaximumWorldSectionSnapshotBytes &&
            snapshot->size() <= (std::numeric_limits<std::uint32_t>::max)() &&
            CopyInputView(persistContext, originalView))
        {
            using InitializeBorrowedInput = void(__thiscall*)(
                void*, const void*, std::uint32_t);
            const auto initialize = reinterpret_cast<InitializeBorrowedInput>(
                base + BorrowedInputInitializeRva);
            initialize(static_cast<std::uint8_t*>(persistContext) + 0x30,
                snapshot->data(), static_cast<std::uint32_t>(snapshot->size()));
            applied = true;
        }
        if (exactSectionCall && snapshotProvider_ != nullptr && !applied)
        {
            // Never fall through to the guest's local world state after the
            // authoritative boundary has been admitted. The service reports
            // a fatal load failure through the result sink.
            if (applyResultSink_ != nullptr)
            {
                applyResultSink_(loadContext_, section, false);
            }
            return;
        }
        original(manager, persistContext);
        if (applied)
        {
            RestoreInputView(persistContext, originalView);
        }
        if (exactSectionCall && snapshotProvider_ != nullptr &&
            applyResultSink_ != nullptr)
        {
            applyResultSink_(loadContext_, section, applied);
        }
    }

    void __fastcall WorldSectionPersistenceHook::SaveRegionsObserved(
        void* const manager, void*, void* const context)
    {
        if (active_ != nullptr)
            active_->ObserveSave(Section::Regions,
                active_->saveRegionsOriginal_, manager, context);
    }

    void __fastcall WorldSectionPersistenceHook::SaveFactionsObserved(
        void* const manager, void*, void* const context)
    {
        if (active_ != nullptr)
            active_->ObserveSave(Section::Factions,
                active_->saveFactionsOriginal_, manager, context);
    }

    void __fastcall WorldSectionPersistenceHook::LoadRegionsObserved(
        void* const manager, void*, void* const context)
    {
        void* const caller = _ReturnAddress();
        if (active_ != nullptr)
            active_->ObserveLoad(Section::Regions,
                active_->loadRegionsOriginal_, RegionLoadReturnRva,
                manager, context, caller);
    }

    void __fastcall WorldSectionPersistenceHook::LoadFactionsObserved(
        void* const manager, void*, void* const context)
    {
        void* const caller = _ReturnAddress();
        if (active_ != nullptr)
            active_->ObserveLoad(Section::Factions,
                active_->loadFactionsOriginal_, FactionLoadReturnRva,
                manager, context, caller);
    }
}
