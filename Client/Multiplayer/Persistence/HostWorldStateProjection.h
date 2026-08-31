#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace fable::game::entity::persistence
{
    class ThingSaveProjectionHook;
}

namespace fable::multiplayer::entities
{
    class EntityLifecycleReplication;
    class EntityNetworkIdentityRegistry;
}

namespace fable::multiplayer::persistence
{
    // Applies the host-authoritative persistent entity directory at native
    // Thing load/save boundaries on every peer. Fable continues to serialize
    // each process's own Hero, so a host or guest save combines one player's
    // character with the same canonical multiplayer world state.
    class HostWorldStateProjection final
    {
    public:
        void Initialize(
            PeerRole role,
            entities::EntityLifecycleReplication& lifecycle,
            entities::EntityNetworkIdentityRegistry& identities,
            const core::Diagnostics& diagnostics);
        bool Attach(
            game::entity::persistence::ThingSaveProjectionHook& hook);
        void Refresh();
        void Shutdown() noexcept;

    private:
        struct ScriptIdentity final
        {
            std::string scriptName;
            std::uint16_t definitionIndex = 0;

            [[nodiscard]] bool operator==(
                const ScriptIdentity& other) const noexcept
            {
                return definitionIndex == other.definitionIndex &&
                    scriptName == other.scriptName;
            }
        };

        struct ScriptIdentityHash final
        {
            [[nodiscard]] std::size_t operator()(
                const ScriptIdentity& identity) const noexcept;
        };

        struct ScriptProjection final
        {
            std::uint16_t mapId = 0;
            bool unique = false;
        };

        struct ProjectionSnapshot final
        {
            std::unordered_map<std::uint64_t, std::uint16_t> byUid;
            std::unordered_map<
                ScriptIdentity,
                ScriptProjection,
                ScriptIdentityHash> byScriptIdentity;
        };

        static bool ResolveMapOverride(
            void* context,
            std::uint64_t thingUid,
            std::uint64_t simulationCreatureUid,
            std::uint16_t definitionIndex,
            const char* scriptName,
            std::uint16_t& mapId) noexcept;

        entities::EntityLifecycleReplication* lifecycle_ = nullptr;
        entities::EntityNetworkIdentityRegistry* identities_ = nullptr;
        game::entity::persistence::ThingSaveProjectionHook* hook_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        // Fable may serialize saved Things on its file-writer thread. The
        // callback consumes only this immutable publication and never reads
        // or mutates the session-owned lifecycle/identity registries.
        std::shared_ptr<const ProjectionSnapshot> publishedSnapshot_;
        std::uint64_t lastWorldRevision_ = 0;
        std::uint64_t lastIdentityRevision_ = 0;
        bool projectionPublished_ = false;
    };
}
