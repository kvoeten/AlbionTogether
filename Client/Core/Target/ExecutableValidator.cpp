#include "ExecutableValidator.h"
#include "Core/Bootstrap/ClientRuntimeServices.h"
#include "Core/Bootstrap/FeatureRegistry.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace fable::core::target
{
    namespace
    {
        constexpr DWORD kExpectedTimestamp = 0x545D058C;
        constexpr DWORD kExpectedImageSize = 0x035D5000;
        constexpr DWORD kExpectedEntryPointRva = 0x0236A782;

        constexpr std::uintptr_t kCharStringConstructorRva = 0x012B7800;
        constexpr std::uintptr_t kGetHeroRva = 0x01889940;
        constexpr std::uintptr_t kGetThingWithScriptNameRva = 0x0189DF10;
        constexpr std::uintptr_t kTurnCreatureIntoRva = 0x01898200;
        constexpr std::uintptr_t kScriptThingVtableRva = 0x02A5CBF4;
        constexpr std::uintptr_t kScriptThingDestructorRva = 0x0135C7A7;
        constexpr std::uintptr_t kScriptThingIsNullRva = 0x0135B9E0;
        constexpr std::uintptr_t kScriptThingGetPositionVectorRva = 0x0135B93F;
        constexpr std::uintptr_t kScriptThingGetFacingAngleRva = 0x0135B994;
        constexpr std::uintptr_t kActiveHeroSelectorRva = 0x018FD7D0;
        constexpr std::uintptr_t kActiveHeroCreatureResolverRva = 0x018E3AD0;
        constexpr std::uintptr_t kHeroProgressionHealthGetMaximumRva = 0x019CC6DA;
        constexpr std::uintptr_t kRegionManagerResolveIndexRva = 0x01BC6560;
        constexpr std::uintptr_t kThingPlayerCreatureVtableRva = 0x02B1DBB4;
        constexpr std::uintptr_t kThingPlayerCreatureModifyCombatHealthRva = 0x01B5A520;
        constexpr std::uintptr_t kQuestManagerSaveGameStateRva = 0x01BC4270;
        constexpr std::uintptr_t kQuestManagerLoadGameStateRva = 0x01BC5200;
        constexpr std::uintptr_t kPersistLoadGameStateRva = 0x01BC5EF0;
        constexpr std::uintptr_t kCStringParserConstructorRva = 0x012C0AA0;
        constexpr std::uintptr_t kCStringParserDestructorRva = 0x012C0B90;
        constexpr std::uintptr_t kQuestManagerGlobalRva = 0x03230360;
        constexpr std::uintptr_t kQuestManagerGlobalUseRva = 0x01891E50;

        constexpr std::size_t kScriptThingDestructorVtableIndex = 0;
        constexpr std::size_t kScriptThingIsNullVtableIndex = 77;
        constexpr std::size_t kThingCreatureModifyCombatHealthVtableIndex = 0x100 / sizeof(void*);

        template <std::size_t Size>
        bool BytesMatch(
            const std::uint8_t* address,
            const std::array<std::uint8_t, Size>& expected) noexcept
        {
            for (std::size_t index = 0; index < Size; ++index)
            {
                if (address[index] != expected[index])
                {
                    return false;
                }
            }
            return true;
        }

        template <std::size_t Size>
        bool BytesMatchRelocatedOperand(
            const std::uint8_t* address,
            const std::array<std::uint8_t, Size>& expected,
            const std::size_t operandOffset,
            const std::uintptr_t operandRva,
            const std::uint8_t* moduleBase) noexcept
        {
            if (operandOffset + sizeof(std::uint32_t) > Size)
            {
                return false;
            }
            for (std::size_t index = 0; index < Size; ++index)
            {
                if (index >= operandOffset &&
                    index < operandOffset + sizeof(std::uint32_t))
                {
                    continue;
                }
                if (address[index] != expected[index])
                {
                    return false;
                }
            }
            std::uint32_t actual = 0;
            std::memcpy(&actual, address + operandOffset, sizeof(actual));
            return actual == static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(moduleBase) + operandRva);
        }

        void Report(ValidationLog log, const char* message) noexcept
        {
            if (log != nullptr)
            {
                log(message);
            }
        }
    }

    bool ValidateFableExecutable(HMODULE gameModule, ValidationLog log) noexcept
    {
        if (gameModule == nullptr)
        {
            Report(log, "Target validation failed: the main executable module is unavailable.");
            return false;
        }

        const auto* base = reinterpret_cast<const std::uint8_t*>(gameModule);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            Report(log, "Target validation failed: invalid DOS header.");
            return false;
        }

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt->FileHeader.TimeDateStamp != kExpectedTimestamp ||
            nt->OptionalHeader.SizeOfImage != kExpectedImageSize ||
            nt->OptionalHeader.AddressOfEntryPoint != kExpectedEntryPointRva)
        {
            Report(log, "Target validation failed: this is not the analyzed Fable Anniversary executable.");
            return false;
        }

        constexpr std::array<std::uint8_t, 9> kCharStringConstructorPrefix = {
            0x56, 0x8B, 0xF1, 0xC7, 0x06, 0x00, 0x00, 0x00, 0x00};
        constexpr std::array<std::uint8_t, 2> kGetHeroPrefix = {0x8B, 0x0D};
        constexpr std::array<std::uint8_t, 10> kGetHeroBody = {
            0x8B, 0x81, 0xE4, 0x00, 0x00, 0x00, 0x8B, 0x50, 0x08, 0x52};
        constexpr std::array<std::uint8_t, 3> kScriptNamePrefix = {0x6A, 0xFF, 0x68};
        constexpr std::array<std::uint8_t, 4> kScriptThingDestructorPrefix = {
            0x56, 0x8B, 0xF1, 0xE8};
        constexpr std::array<std::uint8_t, 5> kScriptThingIsNullPrefix = {
            0x8B, 0x49, 0x04, 0x85, 0xC9};
        constexpr std::array<std::uint8_t, 6> kScriptThingStateAccessorPrefix = {
            0x8B, 0x49, 0x04, 0x85, 0xC9, 0x75};
        constexpr std::array<std::uint8_t, 7> kActiveHeroSelectorPrefix = {
            0x8B, 0x41, 0x10, 0x53, 0x55, 0x56, 0x57};
        constexpr std::array<std::uint8_t, 4> kActiveHeroCreatureResolverPrefix = {
            0x83, 0xC1, 0x2C, 0xE9};
        constexpr std::array<std::uint8_t, 13> kHealthMaximumBody = {
            0x8B, 0x80, 0x00, 0x01, 0x00, 0x00, 0x8B,
            0x80, 0xE4, 0x00, 0x00, 0x00, 0xC3};
        constexpr std::array<std::uint8_t, 8> kRegionManagerResolveIndexPrefix = {
            0x53, 0x55, 0x56, 0x8B, 0xF1, 0x8B, 0x4E, 0x38};
        constexpr std::array<std::uint8_t, 20> kCStringParserConstructorPrefix = {
            0x6A, 0xFF, 0x68, 0x71, 0xD9, 0x89, 0x02, 0x64,
            0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x83, 0xEC,
            0x0C, 0x53, 0x56, 0x57};
        constexpr std::array<std::uint8_t, 20> kCStringParserDestructorPrefix = {
            0x6A, 0xFF, 0x68, 0xB6, 0xD9, 0x89, 0x02, 0x64,
            0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x83, 0xEC,
            0x08, 0x55, 0x56, 0x57};
        constexpr std::array<std::uint8_t, 16> kQuestSavePrefix = {
            0x6A, 0xFF, 0x68, 0x30, 0xE3, 0x95, 0x02, 0x64,
            0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x83, 0xEC};
        constexpr std::array<std::uint8_t, 16> kQuestLoadPrefix = {
            0x6A, 0xFF, 0x68, 0x2C, 0xE6, 0x95, 0x02, 0x64,
            0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x83, 0xEC};
        constexpr std::array<std::uint8_t, 16> kPersistLoadPrefix = {
            0x6A, 0xFF, 0x68, 0x70, 0xE6, 0x95, 0x02, 0x64,
            0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x83, 0xEC};
        constexpr std::array<std::uint8_t, 7> kQuestManagerGlobalUsePrefix = {
            0x8B, 0x0D, 0x60, 0x03, 0x63, 0x03, 0xE9};

        if (!BytesMatch(base + kCharStringConstructorRva, kCharStringConstructorPrefix) ||
            !BytesMatch(base + kGetHeroRva, kGetHeroPrefix) ||
            !BytesMatch(base + kGetHeroRva + 6, kGetHeroBody) ||
            !BytesMatch(base + kGetThingWithScriptNameRva, kScriptNamePrefix) ||
            !BytesMatch(base + kTurnCreatureIntoRva, kScriptNamePrefix) ||
            !BytesMatch(base + kScriptThingDestructorRva, kScriptThingDestructorPrefix) ||
            !BytesMatch(base + kScriptThingIsNullRva, kScriptThingIsNullPrefix) ||
            !BytesMatch(base + kScriptThingGetPositionVectorRva, kScriptThingStateAccessorPrefix) ||
            !BytesMatch(base + kScriptThingGetFacingAngleRva, kScriptThingStateAccessorPrefix) ||
            !BytesMatch(base + kActiveHeroSelectorRva, kActiveHeroSelectorPrefix) ||
            !BytesMatch(base + kActiveHeroCreatureResolverRva, kActiveHeroCreatureResolverPrefix) ||
            !BytesMatch(base + kHeroProgressionHealthGetMaximumRva + 5, kHealthMaximumBody) ||
            !BytesMatch(base + kRegionManagerResolveIndexRva, kRegionManagerResolveIndexPrefix))
        {
            Report(log, "Target validation failed: one or more native signatures drifted.");
            return false;
        }

#define FABLE_REQUIRE_RELOCATED_SIGNATURE(name, rva, bytes, offset, operand) \
        if (!BytesMatchRelocatedOperand( \
                base + (rva), (bytes), (offset), (operand), base)) \
        { \
            Report(log, "Target validation failed: " name " signature drifted."); \
            return false; \
        }

        FABLE_REQUIRE_RELOCATED_SIGNATURE(
            "CStringParser constructor", kCStringParserConstructorRva,
            kCStringParserConstructorPrefix, 3, 0x0249D971);
        FABLE_REQUIRE_RELOCATED_SIGNATURE(
            "CStringParser destructor", kCStringParserDestructorRva,
            kCStringParserDestructorPrefix, 3, 0x0249D9B6);
        FABLE_REQUIRE_RELOCATED_SIGNATURE(
            "quest SaveGameState", kQuestManagerSaveGameStateRva,
            kQuestSavePrefix, 3, 0x0255E330);
        FABLE_REQUIRE_RELOCATED_SIGNATURE(
            "quest LoadGameState", kQuestManagerLoadGameStateRva,
            kQuestLoadPrefix, 3, 0x0255E62C);
        FABLE_REQUIRE_RELOCATED_SIGNATURE(
            "persistence LoadGameState", kPersistLoadGameStateRva,
            kPersistLoadPrefix, 3, 0x0255E670);
        FABLE_REQUIRE_RELOCATED_SIGNATURE(
            "quest-manager global", kQuestManagerGlobalUseRva,
            kQuestManagerGlobalUsePrefix, 2, kQuestManagerGlobalRva);

#undef FABLE_REQUIRE_RELOCATED_SIGNATURE

        const auto scriptThingVtable = reinterpret_cast<void* const*>(base + kScriptThingVtableRva);
        if (scriptThingVtable[kScriptThingDestructorVtableIndex] !=
                reinterpret_cast<const void*>(base + kScriptThingDestructorRva) ||
            scriptThingVtable[kScriptThingIsNullVtableIndex] !=
                reinterpret_cast<const void*>(base + kScriptThingIsNullRva))
        {
            Report(log, "Target validation failed: the CScriptThing vtable layout drifted.");
            return false;
        }

        const auto creatureVtable = reinterpret_cast<void* const*>(base + kThingPlayerCreatureVtableRva);
        if (creatureVtable[kThingCreatureModifyCombatHealthVtableIndex] !=
            reinterpret_cast<const void*>(base + kThingPlayerCreatureModifyCombatHealthRva))
        {
            Report(log, "Target validation failed: the CThingPlayerCreature combat-health vtable layout drifted.");
            return false;
        }

        Report(log, "Target executable and transformation signatures validated.");
        return true;
    }
}

namespace
{
    using namespace fable::core::bootstrap;

    bool TargetValidationEnabled(const FeatureContext&) noexcept { return true; }

    bool InstallTargetValidation(FeatureContext& context) noexcept
    {
        if (!IsPreResumeStage(context))
        {
            return true;
        }
        CoreContext().gameModule = GetModuleHandleW(nullptr);
        return fable::core::target::ValidateFableExecutable(CoreContext().gameModule, Log);
    }

    void UninstallTargetValidation(FeatureContext&) noexcept {}

    FABLE_FEATURE_DEPENDENCIES(fableTargetValidationDependencies, "core.diagnostics");
    FABLE_FEATURE_DESCRIPTOR(
        fableTargetValidationFeature,
        "target.validation",
        "Fable target validation",
        FeaturePhase::Process,
        10,
        TargetValidationEnabled,
        fableTargetValidationDependencies,
        std::size(fableTargetValidationDependencies),
        InstallTargetValidation,
        UninstallTargetValidation,
        "target-executable-validation");
}
