#include "Core/Hooking/CodePatch.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
    int failures = 0;

    void Check(
        const bool condition,
        const char* expression,
        const char* test) noexcept
    {
        if (condition)
        {
            return;
        }
        ++failures;
        std::cerr << test << ": " << expression << '\n';
    }

#define CHECK(test, expression) Check((expression), #expression, (test))

    void TestCompetingPatchIsNeverOverwritten()
    {
        constexpr const char* test = "competing code patch ownership";
        std::array<std::uint8_t, 8> target = {
            0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
        const auto original = target;
        const std::array<std::uint8_t, 8> firstBytes = {
            0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97};
        const std::array<std::uint8_t, 8> secondBytes = {
            0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7};

        fable::core::hooking::CodePatch first;
        fable::core::hooking::CodePatch second;
        CHECK(test, first.Install(
            target.data(), original.data(), original.size(),
            firstBytes.data(), firstBytes.size()));
        CHECK(test, target == firstBytes);
        CHECK(test, second.Install(
            target.data(), firstBytes.data(), firstBytes.size(),
            secondBytes.data(), secondBytes.size()));
        CHECK(test, target == secondBytes);

        CHECK(test, !first.Shutdown());
        CHECK(test, first.UninstallWasSkipped());
        CHECK(test, target == secondBytes);

        CHECK(test, second.Shutdown());
        CHECK(test, target == firstBytes);
        CHECK(test, first.Shutdown());
        CHECK(test, target == original);
    }

    void TestExpectedBytesAreRequired()
    {
        constexpr const char* test = "code patch expected bytes";
        std::array<std::uint8_t, 4> target = {0x01, 0x02, 0x03, 0x04};
        const auto original = target;
        const std::array<std::uint8_t, 4> wrongExpected = {
            0x01, 0x02, 0x03, 0xFF};
        const std::array<std::uint8_t, 4> replacement = {
            0x11, 0x12, 0x13, 0x14};

        fable::core::hooking::CodePatch patch;
        CHECK(test, !patch.Install(
            target.data(), wrongExpected.data(), wrongExpected.size(),
            replacement.data(), replacement.size()));
        CHECK(test, target == original);
        CHECK(test, !patch.IsInstalled());
    }

    void RelativeJumpTarget() noexcept
    {
    }

    void TestRelativeJumpConstructionIsOwned()
    {
        constexpr const char* test = "relative jump code patch";
        std::array<std::uint8_t, 7> target = {
            0x8B, 0x10, 0x8B, 0xC8, 0x8B, 0x42, 0x08};
        const auto original = target;

        fable::core::hooking::CodePatch patch;
        CHECK(test, patch.InstallRelativeJump(
            target.data(),
            original.data(),
            original.size(),
            reinterpret_cast<void*>(&RelativeJumpTarget),
            target.size()));
        CHECK(test, target[0] == 0xE9);
        CHECK(test, target[5] == 0x90);
        CHECK(test, target[6] == 0x90);
        CHECK(test, patch.Shutdown());
        CHECK(test, target == original);
    }
}

int RunCodePatchTests()
{
    TestCompetingPatchIsNeverOverwritten();
    TestExpectedBytesAreRequired();
    TestRelativeJumpConstructionIsOwned();
    return failures;
}
