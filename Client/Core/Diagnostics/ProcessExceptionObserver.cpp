#include "ProcessExceptionObserver.h"
#include "Core/Bootstrap/ClientRuntimeServices.h"
#include "Core/Bootstrap/FeatureRegistry.h"
#include "Core/Target/FableNativeLayout.h"

#include <Windows.h>
#include <algorithm>
#include <cstdio>

namespace fable::core::diagnostics
{
using namespace fable::core::bootstrap;
using namespace fable::core::target;

    namespace
    {
#if defined(_M_IX86)
        void LogStackCodeCandidates(const CONTEXT& context)
        {
            // Optimized retail frames omit EBP. Save bounded raw return-address
            // candidates before SEH unwinds the *first* fault; a later Steam
            // dump may contain only the secondary simulation-thread failure.
            // These are candidates, not a reconstructed call stack.
            DWORD words[1'024] = {};
            MEMORY_BASIC_INFORMATION region = {};
            const auto stack = static_cast<std::uintptr_t>(context.Esp);
            if (VirtualQuery(reinterpret_cast<void*>(stack), &region,
                    sizeof(region)) != sizeof(region) ||
                region.State != MEM_COMMIT ||
                (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            {
                return;
            }
            const auto end = reinterpret_cast<std::uintptr_t>(
                region.BaseAddress) + region.RegionSize;
            if (stack >= end) return;
            const std::size_t bytes = (std::min<std::size_t>)(
                sizeof(words), end - stack);
            SIZE_T read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<const void*>(stack), words, bytes, &read))
            {
                return;
            }

            const auto gameBase = reinterpret_cast<std::uintptr_t>(
                CoreContext().gameModule);
            const auto clientBase = reinterpret_cast<std::uintptr_t>(
                CoreContext().clientModule);
            const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(
                clientBase);
            const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                clientBase + dos->e_lfanew);
            const std::size_t clientSize = nt->OptionalHeader.SizeOfImage;
            unsigned int reported = 0;
            for (std::size_t index = 0; index < read / sizeof(DWORD) &&
                    reported < 48; ++index)
            {
                const std::uintptr_t address = words[index];
                const bool game = address >= gameBase &&
                    address - gameBase < kExpectedImageSize;
                const bool client = address >= clientBase &&
                    address - clientBase < clientSize;
                if (!game && !client) continue;
                MEMORY_BASIC_INFORMATION code = {};
                if (VirtualQuery(reinterpret_cast<const void*>(address),
                        &code, sizeof(code)) != sizeof(code) ||
                    (code.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0)
                {
                    continue; // vtables/global data are not return addresses
                }
                LogFormat(
                    "Process exception code candidate: stack_offset=0x%03X module=%s rva=0x%08lX address=%08lX.",
                    static_cast<unsigned int>(index * sizeof(DWORD)),
                    game ? "game" : "client",
                    static_cast<unsigned long>(
                        address - (game ? gameBase : clientBase)),
                    words[index]);
                ++reported;
            }
        }
#endif
    }

    LONG CaptureNativeFault(
        EXCEPTION_POINTERS* exceptionPointers,
        NativeFault* fault,
        const char* stage)
    {
        fault->stage = stage;
        if (exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr)
        {
            const EXCEPTION_RECORD* record = exceptionPointers->ExceptionRecord;
            fault->code = record->ExceptionCode;
            fault->instructionAddress = record->ExceptionAddress;
            if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                record->NumberParameters >= 2)
            {
                fault->operation = record->ExceptionInformation[0];
                fault->accessedAddress = reinterpret_cast<void*>(record->ExceptionInformation[1]);
            }
        }
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void LogNativeFault(const NativeFault& fault)
    {
        const auto base = reinterpret_cast<std::uintptr_t>(CoreContext().gameModule);
        const auto instruction = reinterpret_cast<std::uintptr_t>(fault.instructionAddress);
        const bool isInsideGame = instruction >= base &&
            instruction < base + kExpectedImageSize;

        wchar_t modulePath[MAX_PATH] = {};
        MEMORY_BASIC_INFORMATION memory = {};
        if (fault.instructionAddress != nullptr &&
            VirtualQuery(fault.instructionAddress, &memory, sizeof(memory)) == sizeof(memory))
        {
            GetModuleFileNameW(
                static_cast<HMODULE>(memory.AllocationBase),
                modulePath,
                static_cast<DWORD>(std::size(modulePath)));
        }

        std::string gameRva = "<outside-game>";
        if (isInsideGame)
        {
            char buffer[32] = {};
            std::snprintf(
                buffer,
                std::size(buffer),
                "0x%08lX",
                static_cast<unsigned long>(instruction - base));
            gameRva = buffer;
        }
        std::string module = WideToUtf8(modulePath);
        if (module.empty())
        {
            module = "<unknown>";
        }

        LogFormat(
            "Native fault: stage=%s code=0x%08lX instruction=%p game_rva=%s module=%s.",
            fault.stage,
            static_cast<unsigned long>(fault.code),
            fault.instructionAddress,
            gameRva.c_str(),
            module.c_str());

        if (fault.code == EXCEPTION_ACCESS_VIOLATION)
        {
            const char* operation = fault.operation == 0
                ? "read"
                : fault.operation == 1 ? "write" : fault.operation == 8 ? "execute" : "unknown";
            LogFormat(
                "Native fault: access_violation operation=%s address=%p.",
                operation,
                fault.accessedAddress);
        }
    }

    LONG CALLBACK ObserveProcessException(EXCEPTION_POINTERS* exceptionPointers)
    {
        constexpr unsigned int MaximumLoggedProcessExceptions = 8;
        if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const EXCEPTION_RECORD* record = exceptionPointers->ExceptionRecord;
        if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
            record->NumberParameters < 2)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const auto gameBase = reinterpret_cast<std::uintptr_t>(CoreContext().gameModule);
        const auto instruction = reinterpret_cast<std::uintptr_t>(
            record->ExceptionAddress);
        const bool lowAddress = record->ExceptionInformation[1] < 0x1'0000;
        const bool insideGame = gameBase != 0 && instruction >= gameBase &&
            instruction < gameBase + kExpectedImageSize;
        if (!lowAddress && !insideGame)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const unsigned int observation =
            DiagnosticsContext().lowAddressAccessViolationsLogged.fetch_add(1, std::memory_order_relaxed) + 1;
        if (observation > MaximumLoggedProcessExceptions)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        NativeFault fault = {};
        fault.stage = "process-wide low-address access-violation observer";
        fault.code = record->ExceptionCode;
        fault.instructionAddress = record->ExceptionAddress;
        fault.operation = record->ExceptionInformation[0];
        fault.accessedAddress = reinterpret_cast<void*>(record->ExceptionInformation[1]);

        LogFormat(
            "Process exception observer: event=%u thread=%lu scope=%s; continuing Windows exception dispatch.",
            observation,
            static_cast<unsigned long>(GetCurrentThreadId()),
            insideGame ? "game" : "low-address");
        LogNativeFault(fault);

#if defined(_M_IX86)
        if (exceptionPointers->ContextRecord != nullptr)
        {
            const CONTEXT* context = exceptionPointers->ContextRecord;
            LogFormat(
                "Process exception registers: eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX eip=%08lX.",
                context->Eax,
                context->Ebx,
                context->Ecx,
                context->Edx,
                context->Esi,
                context->Edi,
                context->Ebp,
                context->Esp,
                context->Eip);

            // The retail build does not consistently preserve EBP, so a
            // conventional frame walk is unreliable. Preserve the first
            // stack words instead: for leaf helpers this includes the direct
            // return address and gives us the owning native lifecycle path.
            DWORD stackWords[12] = {};
            bool stackReadable = false;
            __try
            {
                const auto* const stack = reinterpret_cast<const DWORD*>(
                    context->Esp);
                for (std::size_t index = 0; index < std::size(stackWords);
                     ++index)
                {
                    stackWords[index] = stack[index];
                }
                stackReadable = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                stackReadable = false;
            }
            if (stackReadable)
            {
                LogFormat(
                    "Process exception stack: +00=%08lX +04=%08lX +08=%08lX +0C=%08lX +10=%08lX +14=%08lX +18=%08lX +1C=%08lX +20=%08lX +24=%08lX +28=%08lX +2C=%08lX.",
                    stackWords[0],
                    stackWords[1],
                    stackWords[2],
                    stackWords[3],
                    stackWords[4],
                    stackWords[5],
                    stackWords[6],
                    stackWords[7],
                    stackWords[8],
                    stackWords[9],
                    stackWords[10],
                    stackWords[11]);
            }
            LogStackCodeCandidates(*context);
        }
#endif

        return EXCEPTION_CONTINUE_SEARCH;
    }

}

namespace
{
    using namespace fable::core::bootstrap;

    bool DiagnosticsEnabled(const FeatureContext&) noexcept { return true; }

    bool InstallDiagnostics(FeatureContext& context) noexcept
    {
        if (!IsPreResumeStage(context))
        {
            return true;
        }
        auto& core = CoreContext();
        core.diagnosticLog.Initialize(
            core.clientModule,
            core.configuration.LogPath().c_str(),
            core.configuration.GenerateLogFiles(),
            core.configuration.EventPath().c_str(),
            core.configuration.RunId().c_str(),
            core.configuration.Scenario().c_str());
        if (core.configuration.ShowConsole())
        {
            core.diagnosticLog.AttachConsole();
        }
        Log("AlbionTogether client loaded.");
        LogStartupContext();

        const bool observeFaults =
            core.configuration.Mode() == fable::automation::runtime::ClientMode::TransformProbe ||
            core.configuration.Mode() == fable::automation::runtime::ClientMode::AppearanceCycle ||
            core.configuration.MultiplayerEnabled();
        if (observeFaults)
        {
            DiagnosticsContext().vectoredExceptionHandler = AddVectoredExceptionHandler(
                1, fable::core::diagnostics::ObserveProcessException);
            LogFormat(
                "Hook: low-address access-violation observer %s; handler=%p max_events=8.",
                DiagnosticsContext().vectoredExceptionHandler != nullptr ? "installed" : "failed",
                DiagnosticsContext().vectoredExceptionHandler);
            if (DiagnosticsContext().vectoredExceptionHandler == nullptr)
            {
                return false;
            }
        }
        return true;
    }

    void UninstallDiagnostics(FeatureContext& context) noexcept
    {
        if (!IsPreResumeStage(context))
        {
            return;
        }
        PVOID handler = DiagnosticsContext().vectoredExceptionHandler;
        if (handler != nullptr)
        {
            RemoveVectoredExceptionHandler(handler);
            DiagnosticsContext().vectoredExceptionHandler = nullptr;
        }
        CoreContext().diagnosticLog.Shutdown();
    }

    FABLE_FEATURE_DESCRIPTOR(
        fableDiagnosticsFeature,
        "core.diagnostics",
        "Core diagnostics",
        FeaturePhase::Process,
        0,
        DiagnosticsEnabled,
        nullptr,
        0,
        InstallDiagnostics,
        UninstallDiagnostics,
        "diagnostics-initialization");
}
