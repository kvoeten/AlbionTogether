#include "GameCompatibility.h"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace fable::launcher::diagnostics
{
    namespace
    {
        constexpr std::array<std::uint8_t, 32> SupportedSteamExecutableHash = {
            0x2A, 0x95, 0xEE, 0xA3, 0xC2, 0xCC, 0xE9, 0xB4,
            0x7C, 0xA0, 0xF4, 0x54, 0xA6, 0x05, 0xB6, 0x95,
            0x22, 0x16, 0xF5, 0xD2, 0x51, 0x58, 0xEF, 0xD1,
            0x2B, 0xA4, 0x8B, 0x70, 0x13, 0x09, 0x89, 0xF2};

        bool Sha256FileInternal(
            const std::filesystem::path& path,
            std::array<std::uint8_t, 32>& digest)
        {
            digest.fill(0);
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            HANDLE file = INVALID_HANDLE_VALUE;
            std::vector<std::uint8_t> hashObject;
            bool succeeded = false;

            DWORD objectBytes = 0;
            DWORD resultBytes = 0;
            if (BCryptOpenAlgorithmProvider(
                    &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
                BCryptGetProperty(
                    algorithm,
                    BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&objectBytes),
                    sizeof(objectBytes),
                    &resultBytes,
                    0) < 0 || objectBytes == 0)
            {
                goto cleanup;
            }
            hashObject.resize(objectBytes);
            if (BCryptCreateHash(
                    algorithm,
                    &hash,
                    hashObject.data(),
                    static_cast<ULONG>(hashObject.size()),
                    nullptr,
                    0,
                    0) < 0)
            {
                goto cleanup;
            }

            file = CreateFileW(
                path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_SEQUENTIAL_SCAN,
                nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                goto cleanup;
            }

            {
                // The launcher is x86 and intentionally keeps the default
                // small process stack. Hash large files through a reusable
                // heap buffer instead of placing a megabyte on that stack.
                std::vector<std::uint8_t> buffer(1024 * 1024);
                for (;;)
                {
                    DWORD bytesRead = 0;
                    if (!ReadFile(
                            file,
                            buffer.data(),
                            static_cast<DWORD>(buffer.size()),
                            &bytesRead,
                            nullptr))
                    {
                        goto cleanup;
                    }
                    if (bytesRead == 0)
                    {
                        break;
                    }
                    if (BCryptHashData(hash, buffer.data(), bytesRead, 0) < 0)
                    {
                        goto cleanup;
                    }
                }
            }

            succeeded = BCryptFinishHash(
                hash,
                digest.data(),
                static_cast<ULONG>(digest.size()),
                0) >= 0;

        cleanup:
            if (file != INVALID_HANDLE_VALUE)
            {
                CloseHandle(file);
            }
            if (hash != nullptr)
            {
                BCryptDestroyHash(hash);
            }
            if (algorithm != nullptr)
            {
                BCryptCloseAlgorithmProvider(algorithm, 0);
            }
            return succeeded;
        }
    }

    bool Sha256File(
        const std::filesystem::path& path,
        std::array<std::uint8_t, 32>& digest)
    {
        return Sha256FileInternal(path, digest);
    }

    GameCompatibilityResult CheckGameCompatibility(
        const std::filesystem::path& executable)
    {
        GameCompatibilityResult result;
        result.executable = executable;
        std::error_code error;
        if (executable.empty() ||
            !std::filesystem::is_regular_file(executable, error))
        {
            result.state = GameCompatibilityState::Missing;
            result.detail = L"Fable Anniversary was not found.";
            return result;
        }

        std::array<std::uint8_t, 32> digest = {};
        if (!Sha256File(executable, digest))
        {
            result.state = GameCompatibilityState::Error;
            result.detail = L"The game executable could not be read.";
            return result;
        }
        if (digest != SupportedSteamExecutableHash)
        {
            result.state = GameCompatibilityState::Unsupported;
            result.detail = L"This Fable Anniversary executable is not the supported Steam build.";
            return result;
        }

        result.state = GameCompatibilityState::Compatible;
        result.detail = L"Supported Steam executable detected.";
        return result;
    }
}
