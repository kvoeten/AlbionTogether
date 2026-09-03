#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Entity/Persistence/Native/SavedEntityCompression.h"
#include "Game/Entity/Persistence/SavedEntityMapBlobSnapshot.h"
#include "Game/Entity/Persistence/Serialization/HeroSavedEntityRecord.h"
#include "Multiplayer/Persistence/SavedEntityCollectionBaselineTransfer.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <map>

namespace fable::multiplayer::persistence
{
    // Owns the single source-scoped exception to host save authority: capture
    // the selected guest Hero record, then rewrite a committed host collection
    // so every non-Hero byte remains host-owned.
    class GuestHeroSaveBoundary final
    {
    public:
        bool Initialize(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics) noexcept;
        void Shutdown() noexcept;

        void BeginGuestCollection() noexcept;
        void ObserveGuestRecord(
            const game::entity::persistence::SavedEntityMapBlobSnapshot&
                snapshot) noexcept;
        [[nodiscard]] bool CompleteGuestCollection(bool complete) noexcept;
        [[nodiscard]] bool RewriteHostCollection(
            std::uint64_t collectionRevision,
            std::map<std::uint16_t, SavedEntityCollectionRecord>& records,
            bool insertGuestHero);
        [[nodiscard]] bool RewriteHostRecord(
            SavedEntityCollectionRecord& record,
            bool insertGuestHero);
        [[nodiscard]] bool IsHeroCaptured() const noexcept;
        [[nodiscard]] std::uint16_t SourceMapId() const noexcept;

    private:
        static std::uint64_t HashBytes(
            const std::uint8_t* bytes,
            std::size_t byteCount) noexcept;
        game::entity::persistence::native::SavedEntityCompression compression_;
        game::entity::persistence::serialization::HeroSavedEntityRecord hero_;
        game::entity::persistence::serialization::HeroSavedEntityRecord
            candidateHero_;
        core::Diagnostics diagnostics_ = {};
        std::uint16_t sourceMapId_ = 0;
        std::uint16_t candidateMapId_ = 0;
        std::uint16_t firstHeroMarkerMapId_ = 0;
        std::size_t candidateCount_ = 0;
        std::size_t eligibleRecordCount_ = 0;
        std::size_t inflatedRecordCount_ = 0;
        std::size_t heroMarkerRecordCount_ = 0;
        std::size_t firstHeroMarkerOffset_ = 0;
        bool captureActive_ = false;
        bool captured_ = false;
    };
}
