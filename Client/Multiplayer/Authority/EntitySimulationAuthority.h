#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace fable::game::creature::actions
{
    class CreatureActionLifecycleObserver;
}

namespace fable::game::creature::ai
{
    class AiBrainUpdateObserver;
}

namespace fable::multiplayer::entities
{
    class EntityLifecycleReplication;
    class EntityNetworkIdentityRegistry;
    class EntityPresenceReplication;
}

namespace fable::multiplayer::authority
{
    class AuthorityReplication;

    // Applies the same fenced publisher decision to native AI and native
    // action submission that movement replication already uses. A non-owner
    // keeps the retail creature body, physics, animation, and action playback
    // surface, but cannot make autonomous decisions or originate actions.
    class EntitySimulationAuthority final
    {
    public:
        void Initialize(
            std::uint64_t localActorId,
            AuthorityReplication& authority,
            entities::EntityLifecycleReplication& lifecycle,
            entities::EntityNetworkIdentityRegistry& identities,
            entities::EntityPresenceReplication& presence,
            const core::Diagnostics& diagnostics);
        bool AttachBrainObserver(
            game::creature::ai::AiBrainUpdateObserver& observer);
        bool AttachActionObserver(
            game::creature::actions::CreatureActionLifecycleObserver& observer);
        void Refresh(
            const std::string& localMap,
            bool ownerRosterReady);
        void Shutdown() noexcept;

    private:
        struct DecisionSnapshot final
        {
            struct Decision final
            {
                std::uint64_t nativeUid = 0;
                bool canSimulate = true;
            };

            std::unordered_map<void*, Decision> byCreature;
        };

        static bool ShouldExecuteBrain(
            void* context,
            void* ownerThing) noexcept;
        static bool ShouldSubmitAction(
            void* context,
            void* creature,
            void* action) noexcept;
        [[nodiscard]] bool CanSimulate(void* creature) const noexcept;

        AuthorityReplication* authority_ = nullptr;
        entities::EntityLifecycleReplication* lifecycle_ = nullptr;
        entities::EntityNetworkIdentityRegistry* identities_ = nullptr;
        entities::EntityPresenceReplication* presence_ = nullptr;
        game::creature::ai::AiBrainUpdateObserver* brainObserver_ = nullptr;
        game::creature::actions::CreatureActionLifecycleObserver*
            actionObserver_ = nullptr;
        std::shared_ptr<const DecisionSnapshot> decisions_;
        core::Diagnostics diagnostics_ = {};
        std::uint64_t localActorId_ = 0;
        std::string reportedCoverageMap_;
        std::size_t reportedCreatureCount_ = 0;
        std::size_t reportedPlayerPresentationCount_ = 0;
        std::size_t reportedReplicableCount_ = 0;
        std::size_t reportedLocalSimulationCount_ = 0;
        std::size_t reportedFencedCount_ = 0;
        bool reportedOwnerRosterReady_ = false;
        bool coverageReported_ = false;
    };
}
