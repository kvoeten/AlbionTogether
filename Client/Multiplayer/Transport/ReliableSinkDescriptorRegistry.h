#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstdint>

namespace fable::multiplayer
{
    struct MultiplayerSessionContexts;
    class ReliableMessageDispatcher;
    class ReliableMessageSink;

    using ReliableSinkResolver = ReliableMessageSink* (*)(
        MultiplayerSessionContexts&) noexcept;

    struct ReliableSinkDescriptor final
    {
        std::uint32_t id = 0;
        const char* name = nullptr;
        std::uint32_t order = 0;
        const char* failureDetail = nullptr;
        ReliableSinkResolver resolve = nullptr;
    };

    class ReliableSinkDescriptorRegistry final
    {
    public:
        static bool RegisterDiscovered(
            MultiplayerSessionContexts& contexts,
            ReliableMessageDispatcher& dispatcher,
            const core::Diagnostics& diagnostics);
    };
}

#define FABLE_DETAIL_STRINGIZE_IMPL(value) #value
#define FABLE_DETAIL_STRINGIZE(value) FABLE_DETAIL_STRINGIZE_IMPL(value)

#if defined(_M_IX86)
#define FABLE_DETAIL_FORCE_INCLUDE(symbol)                                  \
    __pragma(comment(linker, "/include:_" FABLE_DETAIL_STRINGIZE(symbol)))
#else
#define FABLE_DETAIL_FORCE_INCLUDE(symbol)                                  \
    __pragma(comment(linker, "/include:" FABLE_DETAIL_STRINGIZE(symbol)))
#endif

#pragma section(".ftrsink$M", read)

#define FABLE_RELIABLE_SINK_DESCRIPTOR(                                    \
    symbol, stableId, stableName, stableOrder, failureIdentity, resolver)   \
    namespace                                                               \
    {                                                                       \
        const ::fable::multiplayer::ReliableSinkDescriptor                  \
            symbol##_descriptor = {                                         \
                stableId, stableName, stableOrder, failureIdentity, resolver}; \
    }                                                                       \
    extern "C"                                                             \
    {                                                                       \
        __declspec(allocate(".ftrsink$M"))                                 \
        extern const ::fable::multiplayer::ReliableSinkDescriptor* const    \
            symbol = &symbol##_descriptor;                                  \
    }                                                                       \
    FABLE_DETAIL_FORCE_INCLUDE(symbol)
