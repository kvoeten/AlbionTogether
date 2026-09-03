#include "SavedEntityCompression.h"

#include <array>
#include <cstring>
#include <limits>

namespace
{
    constexpr std::uintptr_t InflateInitRva = 0x01F92C80u;
    constexpr std::uintptr_t InflateRva = 0x01F92DC0u;
    constexpr std::uintptr_t InflateEndRva = 0x01F940D0u;
    constexpr std::uintptr_t Compress2Rva = 0x02003150u;
    constexpr std::uintptr_t CompressBoundRva = 0x02003210u;
    constexpr std::size_t MaximumCellBytes = 8 * 1024 * 1024;
    constexpr int ZOk = 0;
    constexpr int ZStreamEnd = 1;
    constexpr int ZFinish = 4;
    constexpr int ZDefaultCompression = -1;
    constexpr char ZlibVersion[] = "1.2.1";

    template <std::size_t Size>
    bool Matches(
        const std::uint8_t* const address,
        const std::array<std::uint8_t, Size>& expected) noexcept
    {
        if (address == nullptr)
        {
            return false;
        }
        bool matched = false;
        __try
        {
            matched = std::memcmp(address, expected.data(), Size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            matched = false;
        }
        return matched;
    }
}

namespace fable::game::entity::persistence::native
{
#pragma pack(push, 4)
    struct SavedEntityCompression::ZStream final
    {
        const std::uint8_t* nextIn = nullptr;
        unsigned int availableIn = 0;
        unsigned long totalIn = 0;
        std::uint8_t* nextOut = nullptr;
        unsigned int availableOut = 0;
        unsigned long totalOut = 0;
        const char* message = nullptr;
        void* state = nullptr;
        void* allocate = nullptr;
        void* release = nullptr;
        void* opaque = nullptr;
        int dataType = 0;
        unsigned long adler = 0;
        unsigned long reserved = 0;
    };
#pragma pack(pop)

    bool SavedEntityCompression::Initialize(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics) noexcept
    {
        static_assert(sizeof(ZStream) == 0x38);
        Shutdown();
        diagnostics_ = diagnostics;
        if (gameModule == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const inflateInitAddress = reinterpret_cast<std::uint8_t*>(
            base + InflateInitRva);
        auto* const inflateAddress = reinterpret_cast<std::uint8_t*>(
            base + InflateRva);
        auto* const inflateEndAddress = reinterpret_cast<std::uint8_t*>(
            base + InflateEndRva);
        auto* const compress2Address = reinterpret_cast<std::uint8_t*>(
            base + Compress2Rva);
        auto* const compressBoundAddress = reinterpret_cast<std::uint8_t*>(
            base + CompressBoundRva);
        if (!Matches(inflateInitAddress,
                std::array<std::uint8_t, 12>{
                    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x10,
                    0x8B, 0x4D, 0x0C, 0x8B, 0x55, 0x08}) ||
            !Matches(inflateAddress,
                std::array<std::uint8_t, 12>{
                    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08,
                    0x83, 0xEC, 0x30, 0x57, 0x85, 0xC0}) ||
            !Matches(inflateEndAddress,
                std::array<std::uint8_t, 12>{
                    0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75,
                    0x08, 0x85, 0xF6, 0x74, 0x3B, 0x8B}) ||
            !Matches(compress2Address,
                std::array<std::uint8_t, 12>{
                    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x38,
                    0x8B, 0x4D, 0x14, 0x8B, 0x55, 0x08}) ||
            !Matches(compressBoundAddress,
                std::array<std::uint8_t, 12>{
                    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08,
                    0x8B, 0xC8, 0x8B, 0xD0, 0xC1, 0xE9}))
        {
            diagnostics_.Event(
                "SavedEntityCompressionUnavailable",
                "current-build zlib signatures failed validation");
            Shutdown();
            return false;
        }

        inflateInit_ = reinterpret_cast<InflateInitPointer>(
            inflateInitAddress);
        inflate_ = reinterpret_cast<InflatePointer>(inflateAddress);
        inflateEnd_ = reinterpret_cast<InflateEndPointer>(inflateEndAddress);
        compress2_ = reinterpret_cast<Compress2Pointer>(compress2Address);
        compressBound_ = reinterpret_cast<CompressBoundPointer>(
            compressBoundAddress);
        diagnostics_.Event(
            "SavedEntityCompressionReady",
            "validated the current executable's stateless zlib cell codec");
        return true;
    }

    void SavedEntityCompression::Shutdown() noexcept
    {
        inflateInit_ = nullptr;
        inflate_ = nullptr;
        inflateEnd_ = nullptr;
        compress2_ = nullptr;
        compressBound_ = nullptr;
        diagnostics_ = {};
    }

    bool SavedEntityCompression::Inflate(
        const std::uint8_t* const compressed,
        const std::size_t compressedBytes,
        const std::size_t expectedInflatedBytes,
        std::vector<std::uint8_t>& inflated) const
    {
        inflated.clear();
        if (!IsReady() || compressed == nullptr || compressedBytes == 0 ||
            compressedBytes > MaximumCellBytes || expectedInflatedBytes == 0 ||
            expectedInflatedBytes > MaximumCellBytes ||
            compressedBytes >
                (std::numeric_limits<unsigned int>::max)() ||
            expectedInflatedBytes >
                (std::numeric_limits<unsigned int>::max)())
        {
            return false;
        }
        try
        {
            inflated.resize(expectedInflatedBytes);
        }
        catch (...)
        {
            return false;
        }

        ZStream stream;
        stream.nextIn = compressed;
        stream.availableIn = static_cast<unsigned int>(compressedBytes);
        stream.nextOut = inflated.data();
        stream.availableOut = static_cast<unsigned int>(expectedInflatedBytes);
        const int initialized = inflateInit_(
            &stream, ZlibVersion, static_cast<int>(sizeof(stream)));
        if (initialized != ZOk)
        {
            inflated.clear();
            return false;
        }
        const int result = inflate_(&stream, ZFinish);
        const int ended = inflateEnd_(&stream);
        const bool complete = result == ZStreamEnd && ended == ZOk &&
            stream.totalOut == expectedInflatedBytes &&
            stream.availableIn == 0;
        if (!complete)
        {
            inflated.clear();
        }
        return complete;
    }

    bool SavedEntityCompression::Deflate(
        const std::uint8_t* const inflated,
        const std::size_t inflatedBytes,
        std::vector<std::uint8_t>& compressed) const
    {
        compressed.clear();
        if (!IsReady() || inflated == nullptr || inflatedBytes == 0 ||
            inflatedBytes > MaximumCellBytes ||
            inflatedBytes >
                (std::numeric_limits<unsigned long>::max)())
        {
            return false;
        }
        const auto sourceBytes = static_cast<unsigned long>(inflatedBytes);
        const unsigned long bound = compressBound_(sourceBytes);
        if (bound == 0 || bound > MaximumCellBytes + 64 * 1024)
        {
            return false;
        }
        try
        {
            compressed.resize(bound);
        }
        catch (...)
        {
            return false;
        }
        unsigned long outputBytes = bound;
        const int result = compress2_(
            compressed.data(),
            &outputBytes,
            inflated,
            sourceBytes,
            ZDefaultCompression);
        if (result != ZOk || outputBytes == 0 || outputBytes > bound)
        {
            compressed.clear();
            return false;
        }
        compressed.resize(outputBytes);
        return true;
    }

    bool SavedEntityCompression::IsReady() const noexcept
    {
        return inflateInit_ != nullptr && inflate_ != nullptr &&
            inflateEnd_ != nullptr && compress2_ != nullptr &&
            compressBound_ != nullptr;
    }
}
