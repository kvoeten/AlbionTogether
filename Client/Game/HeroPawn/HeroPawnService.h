#pragma once

#include "../../Core/Diagnostics/Diagnostics.h"
#include "Travel/Hooks/GuildTeleportSafetyHook.h"
#include "Travel/Hooks/MapTransitionUiActionSafetyHook.h"

namespace fable::game
{
    class Entity;
    class EntityService;

    class HeroPawnService final
    {
    public:
        bool Initialize(EntityService& entities, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;

        [[nodiscard]] Entity* Get() const;
        bool SetVisible(Entity* hero, bool visible);

    private:
        EntityService* entities_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        hero_pawn::travel::hooks::GuildTeleportSafetyHook
            guildTeleportSafetyHook_;
        hero_pawn::travel::hooks::MapTransitionUiActionSafetyHook
            mapTransitionUiActionSafetyHook_;
    };
}
