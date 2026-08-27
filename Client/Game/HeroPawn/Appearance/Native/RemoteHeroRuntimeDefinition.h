#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <Windows.h>

namespace fable::game::hero_pawn::appearance::native
{
    // Owns one process-lifetime CThingCreatureDef assembled from retail
    // definitions. The definition is never registered globally or written to
    // disk; callers explicitly substitute it for a scoped actor construction.
    class RemoteHeroRuntimeDefinition final
    {
    public:
        using DefinitionLookup = bool(__thiscall*)(
            void* definitionManager,
            unsigned int definitionIndex,
            void** result);

        [[nodiscard]] bool Ensure(
            HMODULE gameModule,
            DefinitionLookup definitionLookup,
            const core::Diagnostics& diagnostics) noexcept;
        [[nodiscard]] bool ReplaceReference(void** result) noexcept;

        [[nodiscard]] void* Get() const noexcept;
        [[nodiscard]] unsigned int RetailBaseIndex() const noexcept;

        // Fable may retain transient definition references on asynchronous
        // presentation work. Shutdown therefore abandons our owning reference
        // for process lifetime rather than racing those native consumers.
        void AbandonForProcessLifetime() noexcept;

    private:
        void* definition_ = nullptr;
        unsigned int retailBaseIndex_ = 0;
    };
}
