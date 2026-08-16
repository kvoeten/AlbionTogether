#include "HostWorldStateProjection.h"

#include "Game/Entity/Persistence/Hooks/ThingSaveProjectionHook.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"

#include <atomic>
#include <functional>
#include <utility>
#include <vector>

namespace fable::multiplayer::persistence
{
    void HostWorldStateProjection::Initialize(
        PeerRole role,
        entities::EntityLifecycleReplication& lifecycle,
        entities::EntityNetworkIdentityRegistry& identities,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        lifecycle_ = &lifecycle;
        identities_ = &identities;
        diagnostics_ = diagnostics;
        Refresh();
    }

    bool HostWorldStateProjection::Attach(
        game::entity::persistence::ThingSaveProjectionHook& hook)
    {
        hook_ = &hook;
        if (role_ == PeerRole::Host)
        {
            hook_->SetMapOverrideSink(
                &HostWorldStateProjection::ResolveMapOverride,
                this);
            diagnostics_.Event(
                "MultiplayerHostWorldSaveProjectionReady",
                "accepted persistent NPC map state will flow through CThing load and save serialization");
        }
        return hook_->IsInstalled();
    }

    std::size_t HostWorldStateProjection::ScriptIdentityHash::operator()(
        const ScriptIdentity& identity) const noexcept
    {
        const std::size_t text = std::hash<std::string>{}(
            identity.scriptName);
        return text ^ (static_cast<std::size_t>(identity.definitionIndex) +
            0x9e3779b9u + (text << 6) + (text >> 2));
    }

    void HostWorldStateProjection::Refresh()
    {
        auto snapshot = std::make_shared<ProjectionSnapshot>();
        if (role_ == PeerRole::Host && lifecycle_ != nullptr &&
            identities_ != nullptr)
        {
            const std::vector<entities::WorldEntityRecord> records =
                lifecycle_->Directory().Snapshot();
            snapshot->byUid.reserve(records.size() * 2u);
            snapshot->byScriptIdentity.reserve(records.size());
            for (const entities::WorldEntityRecord& record : records)
            {
                if (!record.available || record.mapId == 0 ||
                    (!record.gamePersistent && !record.levelPersistent))
                {
                    continue;
                }
                snapshot->byUid.insert_or_assign(
                    record.thingUid, record.mapId);
                const std::uint64_t localUid =
                    identities_->FindLocal(record.thingUid);
                if (localUid != 0)
                {
                    snapshot->byUid.insert_or_assign(localUid, record.mapId);
                }
                if (record.scriptName.empty())
                {
                    continue;
                }
                ScriptIdentity identity{
                    record.scriptName,
                    record.definitionIndex,
                };
                const auto [match, inserted] =
                    snapshot->byScriptIdentity.emplace(
                        std::move(identity),
                        ScriptProjection{record.mapId, true});
                if (!inserted)
                {
                    // Script identity is a fallback only when exactly one
                    // canonical persistent entity owns it.
                    match->second.unique = false;
                }
            }
        }
        std::shared_ptr<const ProjectionSnapshot> published =
            std::move(snapshot);
        std::atomic_store_explicit(
            &publishedSnapshot_,
            std::move(published),
            std::memory_order_release);
    }

    void HostWorldStateProjection::Shutdown() noexcept
    {
        if (hook_ != nullptr)
        {
            hook_->SetMapOverrideSink(nullptr, nullptr);
        }
        hook_ = nullptr;
        std::shared_ptr<const ProjectionSnapshot> empty =
            std::make_shared<ProjectionSnapshot>();
        std::atomic_store_explicit(
            &publishedSnapshot_,
            std::move(empty),
            std::memory_order_release);
        lifecycle_ = nullptr;
        identities_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
    }

    bool HostWorldStateProjection::ResolveMapOverride(
        void* context,
        std::uint64_t thingUid,
        std::uint16_t definitionIndex,
        const char* scriptName,
        std::uint16_t& mapId) noexcept
    {
        try
        {
            auto* const projection = static_cast<HostWorldStateProjection*>(
                context);
            if (projection == nullptr)
            {
                return false;
            }
            const std::shared_ptr<const ProjectionSnapshot> snapshot =
                std::atomic_load_explicit(
                    &projection->publishedSnapshot_,
                    std::memory_order_acquire);
            if (snapshot == nullptr)
            {
                return false;
            }
            const auto uid = snapshot->byUid.find(thingUid);
            if (uid != snapshot->byUid.end())
            {
                mapId = uid->second;
                return true;
            }
            if (scriptName != nullptr && scriptName[0] != '\0')
            {
                const auto script = snapshot->byScriptIdentity.find(
                    ScriptIdentity{scriptName, definitionIndex});
                if (script != snapshot->byScriptIdentity.end() &&
                    script->second.unique && script->second.mapId != 0)
                {
                    mapId = script->second.mapId;
                    return true;
                }
            }
            return false;
        }
        catch (...)
        {
            return false;
        }
    }
}
