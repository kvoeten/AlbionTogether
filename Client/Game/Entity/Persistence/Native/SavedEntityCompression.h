#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fable::game::entity::persistence::native
{
    // Thin validated view over the zlib 1.2.1 implementation already linked
    // into the supported Fable executable. Saved map records stay in the
    // game's native zlib format without shipping another compression runtime.
    class SavedEntityCompression final
    {
    public:
        bool Initialize(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics) noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool Inflate(
            const std::uint8_t* compressed,
            std::size_t compressedBytes,
            std::size_t expectedInflatedBytes,
            std::vector<std::uint8_t>& inflated) const;
        [[nodiscard]] bool Deflate(
            const std::uint8_t* inflated,
            std::size_t inflatedBytes,
            std::vector<std::uint8_t>& compressed) const;
        [[nodiscard]] bool IsReady() const noexcept;

    private:
        struct ZStream;
        using InflateInitPointer = int(__cdecl*)(ZStream*, const char*, int);
        using InflatePointer = int(__cdecl*)(ZStream*, int);
        using InflateEndPointer = int(__cdecl*)(ZStream*);
        using Compress2Pointer = int(__cdecl*)(
            std::uint8_t*, unsigned long*, const std::uint8_t*,
            unsigned long, int);
        using CompressBoundPointer = unsigned long(__cdecl*)(unsigned long);

        InflateInitPointer inflateInit_ = nullptr;
        InflatePointer inflate_ = nullptr;
        InflateEndPointer inflateEnd_ = nullptr;
        Compress2Pointer compress2_ = nullptr;
        CompressBoundPointer compressBound_ = nullptr;
        core::Diagnostics diagnostics_ = {};
    };
}
