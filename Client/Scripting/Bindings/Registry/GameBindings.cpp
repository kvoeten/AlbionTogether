#include "GameBindings.h"

#include "Game/Creature/Bindings/CreatureBindings.h"
#include "Game/Creature/Bindings/CreatureServiceBindings.h"
#include "Game/Creature/Combat/Bindings/CreatureCombatBindings.h"
#include "Game/Creature/Look/Bindings/CreatureLookBindings.h"
#include "Game/Creature/Locomotion/Bindings/CreatureLocomotionBindings.h"
#include "Game/Entity/Bindings/EntityBindings.h"
#include "Game/HeroPawn/Bindings/HeroPawnBindings.h"
#include "Game/Math/Bindings/MathBindings.h"
#include "Game/NPC/Bindings/NpcBindings.h"
#include "Game/Player/Bindings/PlayerBindings.h"
#include "Game/Quest/Bindings/QuestBindings.h"
#include "Game/World/Bindings/WorldBindings.h"
#include "UI/Hud/Bindings/UiBindings.h"

#include <angelscript.h>

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
        const core::Diagnostics& diagnostics)
    {
        return RegisterMathBindings(engine) &&
            RegisterEntityTypes(engine) &&
            RegisterEntityMembers(engine) &&
            RegisterCreatureBindings(engine) &&
            RegisterCreatureServiceBindings(engine, creatureService) &&
            RegisterCreatureLocomotionBindings(engine, creatureLocomotionService) &&
            RegisterCreatureLookBindings(engine, creatureLookService) &&
            RegisterCreatureCombatBindings(engine, creatureCombatService) &&
            RegisterPlayerBindings(engine, playerService) &&
            RegisterNpcBindings(engine, npcService) &&
            RegisterHeroPawnBindings(engine, heroPawnService) &&
            RegisterQuestBindings(engine, questService) &&
            RegisterWorldBindings(engine, worldService, capabilities, diagnostics) &&
            RegisterUiBindings(engine, hudService);
    }
}
