#pragma once

#include "Multiplayer/Protocol/QuestMutationMessage.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::persistence
{
    enum class QuestMutationAdmission : std::uint8_t
    {
        Accepted,
        Duplicate,
        Invalid,
        WrongAuthority,
        Stale,
        Future,
        ConflictingDuplicate,
        Rejected,
        ApplyFailed,
        StorageFailed,
        RevisionExhausted,
    };

    class QuestMutationAuthority final
    {
    public:
        // Integration must validate quest legality plus the current map-owner
        // or outcome-changing conversation-decision lease before accepting.
        using ValidateSink = bool (*)(
            void* context,
            const protocol::QuestMutationMessage& proposal) noexcept;
        using ApplySink = bool (*)(
            void* context,
            const protocol::QuestMutationMessage& commit) noexcept;

        void Reset(
            std::uint32_t authorityEpoch,
            std::uint64_t sessionRevision,
            std::uint64_t campaignRevision) noexcept;
        void SetValidator(ValidateSink sink, void* context) noexcept;
        void SetApplySink(ApplySink sink, void* context) noexcept;

        [[nodiscard]] QuestMutationAdmission Admit(
            const protocol::QuestMutationMessage& proposal,
            protocol::QuestMutationMessage& commit) noexcept;
        // Host-local retail code already performed this transition. It still
        // passes semantic validation and receives a revision, but the native
        // apply sink must not run a second time.
        [[nodiscard]] QuestMutationAdmission AdmitObserved(
            const protocol::QuestMutationMessage& proposal,
            protocol::QuestMutationMessage& commit) noexcept;

        [[nodiscard]] std::uint64_t CampaignRevision() const noexcept
        {
            return campaignRevision_;
        }

    private:
        static constexpr std::size_t HistoryCapacity = 128;

        struct AcceptedMutation final
        {
            protocol::QuestMutationMessage proposal;
            protocol::QuestMutationMessage commit;
            bool present = false;
        };

        [[nodiscard]] AcceptedMutation* Find(
            std::uint64_t initiatorActorId,
            std::uint64_t mutationId) noexcept;
        void Remember(AcceptedMutation&& mutation) noexcept;
        [[nodiscard]] QuestMutationAdmission AdmitInternal(
            const protocol::QuestMutationMessage& proposal,
            bool alreadyApplied,
            protocol::QuestMutationMessage& commit) noexcept;

        std::array<AcceptedMutation, HistoryCapacity> history_ = {};
        std::size_t nextHistory_ = 0;
        std::uint32_t authorityEpoch_ = 0;
        std::uint64_t sessionRevision_ = 0;
        std::uint64_t campaignRevision_ = 0;
        ValidateSink validator_ = nullptr;
        void* validatorContext_ = nullptr;
        ApplySink apply_ = nullptr;
        void* applyContext_ = nullptr;
    };
}
