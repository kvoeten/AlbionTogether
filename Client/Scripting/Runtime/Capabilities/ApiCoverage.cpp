#include "ApiCoverage.h"

#include "Core/Capabilities/CapabilityRegistry.h"

namespace fable::scripting
{
    namespace
    {
        using core::CapabilityStatus;

        struct ApiCoverageEntry final
        {
            const char* Name;
            CapabilityStatus Status;
            const char* Description;
        };

        constexpr ApiCoverageEntry kApiCoverage[] = {
        {"Runtime.Modules", CapabilityStatus::Verified, "recursive independent AngelScript modules"},
        {"Runtime.Callbacks", CapabilityStatus::Verified, "load, unload, tick, and key callbacks"},
        {"Runtime.Reload", CapabilityStatus::Experimental, "F5 recompiles deployed modules; active native-control reload proof pending"},
        {"Runtime.Events.WorldReady", CapabilityStatus::Verified, "typed world-ready callback exercised by adult-save automation"},
        {"Runtime.Scheduler", CapabilityStatus::Verified, "one-shot and repeating script callbacks with in-callback cancellation exercised by automation"},
        {"Runtime.Events.Bus", CapabilityStatus::Verified, "named native and script events with in-callback unsubscription exercised at WorldReady"},
        {"Runtime.Persistence", CapabilityStatus::Verified, "typed per-module storage scoped by stable relative script path and restored across processes"},
        {"World.FindByScriptName", CapabilityStatus::Verified, "script-name entity lookup"},
        {"World.CreateCreature", CapabilityStatus::Verified, "retail generic-creature creation"},
        {"World.EntityEnumeration", CapabilityStatus::Unavailable, "current-build world collection mapping pending"},
        {"World.Regions", CapabilityStatus::Unavailable, "region state and transition mapping pending"},
        {"World.MapLifecycle", CapabilityStatus::Unavailable, "typed load and unload events pending"},
        {"World.Time", CapabilityStatus::Unavailable, "world time and calendar mapping pending"},
        {"World.Effects", CapabilityStatus::Unavailable, "effect, light, explosion, and barrier mapping pending"},
        {"Entity.State", CapabilityStatus::Verified, "valid, alive, dead, position, and facing"},
        {"Entity.Teleport", CapabilityStatus::Verified, "retail entity teleport call"},
        {"Entity.Flags", CapabilityStatus::Verified, "attackable, damageable, collidable, and drawable flags"},
        {"Entity.Attack", CapabilityStatus::Experimental, "immediate native attack request"},
        {"Entity.Metadata.Read", CapabilityStatus::Verified, "name, definition, data, current-map, and home-map reads exercised by adult-save automation"},
        {"Entity.Metadata.Write", CapabilityStatus::Experimental, "native entity data-string mutation; explicit mod use only"},
        {"Entity.Messages", CapabilityStatus::Unavailable, "interaction and combat message surface pending"},
        {"Entity.Interaction.State", CapabilityStatus::Verified, "usable, open-door, summoned, awareness, activation, and script-counter state"},
        {"Entity.Interaction.Mutation", CapabilityStatus::Experimental, "usable, activation, attachment, and script-counter mutation"},
        {"Entity.Interaction.Messages", CapabilityStatus::Unavailable, "readable, chest, door actions, and interaction messages pending"},
        {"Entity.Attachments", CapabilityStatus::Unavailable, "attachment and carried-item mapping pending"},
        {"Creature.AcquireControl", CapabilityStatus::Verified, "native scripted-control acquisition"},
        {"Creature.Navigation", CapabilityStatus::Verified, "native move and follow actions"},
        {"Creature.Locomotion.State.Read", CapabilityStatus::Experimental, "typed current-build CTCPhysicsNavigator, CTCCreatureNavigation, and CTCAnimationComplex snapshot; automation proof pending"},
        {"Creature.Locomotion.AnimationMotionBridge", CapabilityStatus::Experimental, "validated player/creature frame-update layouts; copies nonzero Hero frame displacement into the watched NPC locomotion-mode input before retail gait selection; visual proof pending"},
        {"Creature.Locomotion.PlayerFrameInputRouter", CapabilityStatus::Verified, "adult-town automation and visual acceptance routed Hero frame displacement through the NPC navigator into retail gait"},
        {"Creature.Locomotion.HeroShadowFollow", CapabilityStatus::Experimental, "moves the hidden non-collidable Hero physics body to the controlled NPC each script tick; current-build proof pending"},
        {"Creature.Look.MovementFacingRouter", CapabilityStatus::Verified, "adult-town automation and visual acceptance suppress autonomous look interest and route movement-derived NPC body facing across proxy forms"},
        {"Creature.Diagnostics.PhysicsWorldPositionMirror", CapabilityStatus::Experimental, "validated slot-32 absolute physics transform mirror; explicitly not a locomotion, facing, or gait API"},
        {"Creature.Animation", CapabilityStatus::Experimental, "native scripted animation actions; direct active-state proof pending"},
        {"Creature.CombatAnimation", CapabilityStatus::Experimental, "native scripted combat animation actions"},
        {"Creature.CombatHealth.Read", CapabilityStatus::Verified, "validated CThingCreature combat-health state"},
        {"Creature.CombatHealth.Write", CapabilityStatus::Experimental, "native combat-health delta mutation"},
        {"Creature.Combat.PlayerInputRouter", CapabilityStatus::Experimental, "intercepts CThingCreature ability submission only from Fable's resolved player ATTACK command handler and substitutes the controlled NPC for the hidden Hero; native weapon sweep and target selection remain owned by the creature stack; runtime proof pending"},
        {"Creature.AI", CapabilityStatus::Unavailable, "brain, behavior, and server-authority policy pending"},
        {"Creature.Perception", CapabilityStatus::Unavailable, "sight, hearing, smell, and awareness mapping pending"},
        {"Creature.Factions", CapabilityStatus::Unavailable, "allies, enemies, affinity, and guard relations pending"},
        {"Creature.CombatPolicy", CapabilityStatus::Unavailable, "targeting, attackers, combat type, and abilities pending"},
        {"Player.Hero", CapabilityStatus::Verified, "SCRIPT_NAME_HERO resolution through the player service"},
        {"Player.CombatHealth.Read", CapabilityStatus::Verified, "Hero CThingPlayerCreature combat-health state"},
        {"Player.CombatHealth.Write", CapabilityStatus::Experimental, "Hero combat-health delta mutation"},
        {"Player.Input", CapabilityStatus::Experimental, "resolved player ATTACK command-to-creature ability interception is available for the controlled NPC combat prototype; raw mouse input is not observed"},
        {"Player.Targeting", CapabilityStatus::Experimental, "target-component inspection is diagnostic only; proxy combat now retains native creature weapon-sweep targeting"},
        {"Player.Progression", CapabilityStatus::Unavailable, "stats, experience, costs, and unlock rules pending"},
        {"HeroPawn.Appearance", CapabilityStatus::Unavailable, "morph, mesh, material, and appearance seeds pending"},
        {"HeroPawn.Equipment", CapabilityStatus::Unavailable, "weapons, clothing, tattoos, hair, and title presentation pending"},
        {"HeroPawn.Age", CapabilityStatus::Unavailable, "age and age-driven presentation mapping pending"},
        {"HeroPawn.Masquerade", CapabilityStatus::Experimental, "Hero-hidden native creature puppet proof"},
        {"HeroPawn.Visibility", CapabilityStatus::Verified, "Hero drawable state through the typed HeroPawn service"},
        {"NPC.Spawn", CapabilityStatus::Verified, "generic native creature creation through World"},
        {"NPC.Authority", CapabilityStatus::Unavailable, "server-owned availability, state, and lifecycle pending"},
        {"NPC.Dialogue", CapabilityStatus::Unavailable, "speech, conversations, and custom dialogue pending"},
        {"NPC.Schedule", CapabilityStatus::Unavailable, "server schedules and residency pending"},
        {"NPC.Shop", CapabilityStatus::Unavailable, "server inventory and price adapters pending"},
        {"Quest.State.Read", CapabilityStatus::Verified, "active, registered, completed, and failed predicates distinguished known retail quests from an unknown quest"},
        {"Quest.State.Write", CapabilityStatus::Unavailable, "activation, completion, failure, and persistence mutation pending isolated-save proof"},
        {"Quest.Objectives", CapabilityStatus::Unavailable, "objectives, HUD, and reward mapping pending"},
        {"Quest.Timers", CapabilityStatus::Unavailable, "timer and countdown mapping pending"},
        {"Quest.Daily", CapabilityStatus::Unavailable, "server-backed daily quest adapter pending"},
        {"Inventory.Items", CapabilityStatus::Unavailable, "item, weapon, clothing, and key mapping pending"},
        {"Inventory.Gold", CapabilityStatus::Unavailable, "gold balance and transfer mapping pending"},
        {"Inventory.Containers", CapabilityStatus::Unavailable, "container enumeration and transfer mapping pending"},
        {"Inventory.Trade", CapabilityStatus::Unavailable, "atomic player and shop trade transaction pending"},
        {"Property.Ownership", CapabilityStatus::Unavailable, "property owner and residency mapping pending"},
        {"Property.Stock", CapabilityStatus::Unavailable, "shop inventory and pricing mapping pending"},
        {"Property.Doors", CapabilityStatus::Unavailable, "door state, lock, and interaction mapping pending"},
        {"Property.Keys", CapabilityStatus::Unavailable, "key identity and authorization adapter pending"},
        {"UI.ShowMessage", CapabilityStatus::Experimental, "retail AddScreenMessage call"},
        {"UI.CustomWidgets", CapabilityStatus::Unavailable, "Scaleform integration pending"},
        {"UI.Nameplates", CapabilityStatus::Unavailable, "world-space player display names pending"},
        {"UI.Inventory", CapabilityStatus::Unavailable, "inventory screen integration pending"},
        {"UI.Trade", CapabilityStatus::Unavailable, "custom trade menu and transaction state pending"},
        {"UI.MainMenu", CapabilityStatus::Experimental, "validated front-end lifecycle automation, script API pending"},
        {"Audio.Sound", CapabilityStatus::Unavailable, "2D and positional game sound mapping pending"},
        {"Audio.Conversation", CapabilityStatus::Unavailable, "conversation playback and subtitles pending"},
        {"Audio.ProximityVoice", CapabilityStatus::Unavailable, "network voice transport and spatial playback pending"},
        {"Save.Load", CapabilityStatus::Verified, "isolated exact AutoSave selection and world-ready automation"},
        {"Save.ServerCharacter", CapabilityStatus::Experimental, "server snapshot transform and combat-health bootstrap"},
        {"Save.CustomPersistence", CapabilityStatus::Unavailable, "server-owned character serialization pending"},
        };
    }

    void RegisterApiCoverage(core::CapabilityRegistry& capabilities)
    {
        for (const auto& entry : kApiCoverage)
        {
            capabilities.Set(entry.Name, entry.Status, entry.Description);
        }
    }
}
