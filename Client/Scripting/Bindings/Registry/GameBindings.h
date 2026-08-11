#pragma once

#include "Core/Diagnostics/Diagnostics.h"

class asIScriptEngine;

namespace fable::game
{
    class CreatureService;
    class EntityService;
    class HeroPawnService;
    class NpcService;
    class PlayerService;
    class QuestService;
    class WorldService;
}

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionService;
}

namespace fable::game::creature::look
{
    class CreatureLookService;
}

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::core
{
    class CapabilityRegistry;
}

namespace fable::ui
{
    class HudService;
}

namespace fable::scripting::bindings
{
    bool RegisterGameBindings(
        asIScriptEngine& engine,
        game::CreatureService& creatureService,
        game::creature::locomotion::CreatureLocomotionService&
            creatureLocomotionService,
        game::creature::look::CreatureLookService& creatureLookService,
        game::creature::combat::CreatureCombatService& creatureCombatService,
        game::PlayerService& playerService,
        game::NpcService& npcService,
        game::HeroPawnService& heroPawnService,
        game::QuestService& questService,
        game::WorldService& worldService,
        ui::HudService& hudService,
        core::CapabilityRegistry& capabilities,
        const core::Diagnostics& diagnostics);
}
