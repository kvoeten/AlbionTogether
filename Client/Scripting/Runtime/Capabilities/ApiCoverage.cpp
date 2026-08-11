#include "ApiCoverage.h"

#include "Core/Capabilities/CapabilityRegistry.h"

namespace fable::scripting
{
    void RegisterApiCoverage(core::CapabilityRegistry& capabilities)
    {
        using core::CapabilityStatus;

        capabilities.Set("Runtime.Modules", CapabilityStatus::Verified, "recursive independent AngelScript modules");
        capabilities.Set("Runtime.Callbacks", CapabilityStatus::Verified, "load, unload, tick, and key callbacks");
        capabilities.Set("Runtime.Reload", CapabilityStatus::Experimental, "F5 recompiles deployed modules; active native-control reload proof pending");
        capabilities.Set("Runtime.Events.WorldReady", CapabilityStatus::Verified, "typed world-ready callback exercised by adult-save automation");
        capabilities.Set("Runtime.Scheduler", CapabilityStatus::Verified, "one-shot and repeating script callbacks with in-callback cancellation exercised by automation");
        capabilities.Set("Runtime.Events.Bus", CapabilityStatus::Verified, "named native and script events with in-callback unsubscription exercised at WorldReady");
        capabilities.Set("Runtime.Persistence", CapabilityStatus::Verified, "typed per-module storage scoped by stable relative script path and restored across processes");

        capabilities.Set("World.FindByScriptName", CapabilityStatus::Verified, "script-name entity lookup");
        capabilities.Set("World.CreateCreature", CapabilityStatus::Verified, "retail generic-creature creation");
        capabilities.Set("World.EntityEnumeration", CapabilityStatus::Unavailable, "current-build world collection mapping pending");
        capabilities.Set("World.Regions", CapabilityStatus::Unavailable, "region state and transition mapping pending");
        capabilities.Set("World.MapLifecycle", CapabilityStatus::Unavailable, "typed load and unload events pending");
        capabilities.Set("World.Time", CapabilityStatus::Unavailable, "world time and calendar mapping pending");
        capabilities.Set("World.Effects", CapabilityStatus::Unavailable, "effect, light, explosion, and barrier mapping pending");

        capabilities.Set("Entity.State", CapabilityStatus::Verified, "valid, alive, dead, position, and facing");
        capabilities.Set("Entity.Teleport", CapabilityStatus::Verified, "retail entity teleport call");
        capabilities.Set("Entity.Flags", CapabilityStatus::Verified, "attackable, damageable, collidable, and drawable flags");
        capabilities.Set("Entity.Attack", CapabilityStatus::Experimental, "immediate native attack request");
        capabilities.Set("Entity.Metadata.Read", CapabilityStatus::Verified, "name, definition, data, current-map, and home-map reads exercised by adult-save automation");
        capabilities.Set("Entity.Metadata.Write", CapabilityStatus::Experimental, "native entity data-string mutation; explicit mod use only");
        capabilities.Set("Entity.Messages", CapabilityStatus::Unavailable, "interaction and combat message surface pending");
        capabilities.Set("Entity.Interaction.State", CapabilityStatus::Verified, "usable, open-door, summoned, awareness, activation, and script-counter state");
        capabilities.Set("Entity.Interaction.Mutation", CapabilityStatus::Experimental, "usable, activation, attachment, and script-counter mutation");
        capabilities.Set("Entity.Interaction.Messages", CapabilityStatus::Unavailable, "readable, chest, door actions, and interaction messages pending");
        capabilities.Set("Entity.Attachments", CapabilityStatus::Unavailable, "attachment and carried-item mapping pending");

        capabilities.Set("Creature.AcquireControl", CapabilityStatus::Verified, "native scripted-control acquisition");
        capabilities.Set("Creature.Navigation", CapabilityStatus::Verified, "native move and follow actions");
        capabilities.Set("Creature.Locomotion.State.Read", CapabilityStatus::Experimental, "typed current-build CTCPhysicsNavigator, CTCCreatureNavigation, and CTCAnimationComplex snapshot; automation proof pending");
        capabilities.Set("Creature.Locomotion.AnimationMotionBridge", CapabilityStatus::Experimental, "validated player/creature frame-update layouts; copies nonzero Hero frame displacement into the watched NPC locomotion-mode input before retail gait selection; visual proof pending");
        capabilities.Set("Creature.Locomotion.PlayerFrameInputRouter", CapabilityStatus::Verified, "adult-town automation and visual acceptance routed Hero frame displacement through the NPC navigator into retail gait");
        capabilities.Set("Creature.Locomotion.HeroShadowFollow", CapabilityStatus::Experimental, "moves the hidden non-collidable Hero physics body to the controlled NPC each script tick; current-build proof pending");
        capabilities.Set("Creature.Look.MovementFacingRouter", CapabilityStatus::Verified, "adult-town automation and visual acceptance suppress autonomous look interest and route movement-derived NPC body facing across proxy forms");
        capabilities.Set("Creature.Diagnostics.PhysicsWorldPositionMirror", CapabilityStatus::Experimental, "validated slot-32 absolute physics transform mirror; explicitly not a locomotion, facing, or gait API");
        capabilities.Set("Creature.Animation", CapabilityStatus::Experimental, "native scripted animation actions; direct active-state proof pending");
        capabilities.Set("Creature.CombatAnimation", CapabilityStatus::Experimental, "native scripted combat animation actions");
        capabilities.Set("Creature.CombatHealth.Read", CapabilityStatus::Verified, "validated CThingCreature combat-health state");
        capabilities.Set("Creature.CombatHealth.Write", CapabilityStatus::Experimental, "native combat-health delta mutation");
        capabilities.Set("Creature.Combat.PlayerInputRouter", CapabilityStatus::Experimental, "intercepts CThingCreature ability submission only from Fable's resolved player ATTACK command handler and substitutes the controlled NPC for the hidden Hero; native weapon sweep and target selection remain owned by the creature stack; runtime proof pending");
        capabilities.Set("Creature.AI", CapabilityStatus::Unavailable, "brain, behavior, and server-authority policy pending");
        capabilities.Set("Creature.Perception", CapabilityStatus::Unavailable, "sight, hearing, smell, and awareness mapping pending");
        capabilities.Set("Creature.Factions", CapabilityStatus::Unavailable, "allies, enemies, affinity, and guard relations pending");
        capabilities.Set("Creature.CombatPolicy", CapabilityStatus::Unavailable, "targeting, attackers, combat type, and abilities pending");

        capabilities.Set("Player.Hero", CapabilityStatus::Verified, "SCRIPT_NAME_HERO resolution through the player service");
        capabilities.Set("Player.CombatHealth.Read", CapabilityStatus::Verified, "Hero CThingPlayerCreature combat-health state");
        capabilities.Set("Player.CombatHealth.Write", CapabilityStatus::Experimental, "Hero combat-health delta mutation");
        capabilities.Set("Player.Input", CapabilityStatus::Experimental, "resolved player ATTACK command-to-creature ability interception is available for the controlled NPC combat prototype; raw mouse input is not observed");
        capabilities.Set("Player.Targeting", CapabilityStatus::Experimental, "target-component inspection is diagnostic only; proxy combat now retains native creature weapon-sweep targeting");
        capabilities.Set("Player.Progression", CapabilityStatus::Unavailable, "stats, experience, costs, and unlock rules pending");

        capabilities.Set("HeroPawn.Appearance", CapabilityStatus::Unavailable, "morph, mesh, material, and appearance seeds pending");
        capabilities.Set("HeroPawn.Equipment", CapabilityStatus::Unavailable, "weapons, clothing, tattoos, hair, and title presentation pending");
        capabilities.Set("HeroPawn.Age", CapabilityStatus::Unavailable, "age and age-driven presentation mapping pending");
        capabilities.Set("HeroPawn.Masquerade", CapabilityStatus::Experimental, "Hero-hidden native creature puppet proof");
        capabilities.Set("HeroPawn.Visibility", CapabilityStatus::Verified, "Hero drawable state through the typed HeroPawn service");

        capabilities.Set("NPC.Spawn", CapabilityStatus::Verified, "generic native creature creation through World");
        capabilities.Set("NPC.Authority", CapabilityStatus::Unavailable, "server-owned availability, state, and lifecycle pending");
        capabilities.Set("NPC.Dialogue", CapabilityStatus::Unavailable, "speech, conversations, and custom dialogue pending");
        capabilities.Set("NPC.Schedule", CapabilityStatus::Unavailable, "server schedules and residency pending");
        capabilities.Set("NPC.Shop", CapabilityStatus::Unavailable, "server inventory and price adapters pending");

        capabilities.Set("Quest.State.Read", CapabilityStatus::Verified, "active, registered, completed, and failed predicates distinguished known retail quests from an unknown quest");
        capabilities.Set("Quest.State.Write", CapabilityStatus::Unavailable, "activation, completion, failure, and persistence mutation pending isolated-save proof");
        capabilities.Set("Quest.Objectives", CapabilityStatus::Unavailable, "objectives, HUD, and reward mapping pending");
        capabilities.Set("Quest.Timers", CapabilityStatus::Unavailable, "timer and countdown mapping pending");
        capabilities.Set("Quest.Daily", CapabilityStatus::Unavailable, "server-backed daily quest adapter pending");

        capabilities.Set("Inventory.Items", CapabilityStatus::Unavailable, "item, weapon, clothing, and key mapping pending");
        capabilities.Set("Inventory.Gold", CapabilityStatus::Unavailable, "gold balance and transfer mapping pending");
        capabilities.Set("Inventory.Containers", CapabilityStatus::Unavailable, "container enumeration and transfer mapping pending");
        capabilities.Set("Inventory.Trade", CapabilityStatus::Unavailable, "atomic player and shop trade transaction pending");

        capabilities.Set("Property.Ownership", CapabilityStatus::Unavailable, "property owner and residency mapping pending");
        capabilities.Set("Property.Stock", CapabilityStatus::Unavailable, "shop inventory and pricing mapping pending");
        capabilities.Set("Property.Doors", CapabilityStatus::Unavailable, "door state, lock, and interaction mapping pending");
        capabilities.Set("Property.Keys", CapabilityStatus::Unavailable, "key identity and authorization adapter pending");

        capabilities.Set("UI.ShowMessage", CapabilityStatus::Experimental, "retail AddScreenMessage call");
        capabilities.Set("UI.CustomWidgets", CapabilityStatus::Unavailable, "Scaleform integration pending");
        capabilities.Set("UI.Nameplates", CapabilityStatus::Unavailable, "world-space player display names pending");
        capabilities.Set("UI.Inventory", CapabilityStatus::Unavailable, "inventory screen integration pending");
        capabilities.Set("UI.Trade", CapabilityStatus::Unavailable, "custom trade menu and transaction state pending");
        capabilities.Set("UI.MainMenu", CapabilityStatus::Experimental, "validated front-end lifecycle automation, script API pending");

        capabilities.Set("Audio.Sound", CapabilityStatus::Unavailable, "2D and positional game sound mapping pending");
        capabilities.Set("Audio.Conversation", CapabilityStatus::Unavailable, "conversation playback and subtitles pending");
        capabilities.Set("Audio.ProximityVoice", CapabilityStatus::Unavailable, "network voice transport and spatial playback pending");

        capabilities.Set("Save.Load", CapabilityStatus::Verified, "isolated exact AutoSave selection and world-ready automation");
        capabilities.Set("Save.ServerCharacter", CapabilityStatus::Experimental, "server snapshot transform and combat-health bootstrap");
        capabilities.Set("Save.CustomPersistence", CapabilityStatus::Unavailable, "server-owned character serialization pending");
    }
}
