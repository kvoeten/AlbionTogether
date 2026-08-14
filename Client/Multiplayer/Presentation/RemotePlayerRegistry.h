#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/HeroPawn/Appearance/Hooks/RemoteHeroPresentationFactoryHook.h"
#include "Multiplayer/Presentation/RemotePlayerPresentation.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fable::game
{
    class Entity;
    class EntityService;
    class NpcService;
}

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionService;
}

namespace fable::game::creature::look
{
    class CreatureLookService;
}

namespace fable::multiplayer::presentation
{
    // Dynamically owns one native presentation per live remote actor ID. The
    // shared factory hook is process-global; individual actor state is not.
    class RemotePlayerRegistry final
    {
    public:
        bool Initialize(
            game::EntityService& entities,
            game::NpcService& npcs,
            game::creature::locomotion::CreatureLocomotionService& locomotion,
            game::creature::look::CreatureLookService& look,
            const core::Diagnostics& diagnostics,
            std::uint64_t localActorId);
        void Reconcile(
            const std::vector<replication::RemotePlayerSnapshot>& snapshots,
            const std::string& localMap,
            game::Entity* localHero);
        void Remove(std::uint64_t actorId) noexcept;
        void BeginWorldTransition() noexcept;
        void CompleteWorldTransition() noexcept;
        void DriveMovement();
        void Shutdown() noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] std::size_t ActiveCount() const;

    private:
        std::unique_ptr<RemotePlayerPresentation> CreatePresentation();

        game::EntityService* entities_ = nullptr;
        game::NpcService* npcs_ = nullptr;
        game::creature::locomotion::CreatureLocomotionService* locomotion_ =
            nullptr;
        game::creature::look::CreatureLookService* look_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        game::hero_pawn::appearance::hooks::RemoteHeroPresentationFactoryHook
            presentationFactory_;
        std::unordered_map<
            std::uint64_t,
            std::unique_ptr<RemotePlayerPresentation>> presentations_;
        std::uint64_t localActorId_ = 0;
        bool initialized_ = false;
    };
}
