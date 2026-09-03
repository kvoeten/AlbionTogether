# AlbionTogether scripting framework

## Target

AlbionTogether treats Fable Script Extender (FSE) breadth as the compatibility
benchmark. The framework is not a purpose-built multiplayer shim. Mods should
eventually be able to implement the RP game mode, daily quests, NPC dialogue,
shops, property and door rules, progression, appearance, policing, and custom
UI without adding one-off C++ hooks for every feature.

The local FSE reference exposes 85 distinct entity methods and 844 distinct
quest-state methods. Those totals are a coverage baseline, not an instruction
to copy the Lua API literally. AlbionTogether groups the same engine concerns
into typed AngelScript services and makes ABI confidence visible at runtime.

## Service layout

### Callback threads

`OnTick`, `OnKeyPressed`, and `OnWorldReady` execute after the native simulation
frame. Window messages enqueue bounded value requests; they do not reconcile
multiplayer actors or change native movement/components. Developer-tool commands
use the same simulation dispatch. The native save-load gate retains its control
message pump while ordinary frames cannot advance.

`OnGui` remains on the DX9 render callback. Use it to draw UI and enqueue
`DevTools` commands, not to call native gameplay mutation APIs directly. The
AngelScript execution lock protects the VM; it is not an engine-thread lock.
Module initialization remains bootstrap work and must not mutate a live world.

The simulation hook is installed before the game resumes. Runtime shutdown
detaches its consumer and waits for an in-flight callback; its trampoline and
containing DLL remain resident until process exit. Detachment is not proof that
all other native/render callbacks are quiescent.

### Native ownership

Native code is organized around the game concept that owns it:

- `Game/World`: world readiness, region/map transitions, lookup, creation, and
  entity lifecycle.
- `Game/Entity`: stable handles, transforms, flags, interaction messages, and
  generic entity state.
- `Game/Creature`: combat health, scripted movement, animation, combat actions,
  perception, factions, and AI policy.
- `Game/Player`: the authoritative player identity, combat state, input, and
  player-only rules.
- `Game/HeroPawn`: Hero presentation, morphs, equipment, hair, tattoos, age,
  morality, renown, expressions, and proxy/masquerade behavior.
- `Game/NPC`: server-owned NPC spawning, availability, dialogue, schedules,
  shops, and behavior policy.
- `Game/Quest`: quest registration/state/objectives/rewards, timers, persistent
  script state, and daily-quest adapters.
- `Game/Inventory`: items, weapons, clothing, keys, gold, containers, and
  transfers.
- `Game/Property`: ownership, residency, prices, stock, doors, locks, and key
  authorization.
- `UI/MainWindow`, `UI/MainMenu`, `UI/Inventory`, `UI/Trade`, and `UI/Hud`:
  lifecycle hooks and script-facing presentation.
- `Audio`: music, effects, conversations, and later proximity-voice routing.
- `Scripting`: module discovery, compilation, callbacks, events, scheduling,
  persistence, hot reload, and capability discovery.

This prevents the injected DLL entry point from becoming the gameplay API.
Target addresses, ABI declarations, and signature validation belong in the
owning domain's `Native` folder. Hook callbacks and installation state belong
in that domain's separate `Hooks` folder; only reusable patch primitives belong
under `Core/Hooking`. Scripts never receive raw engine pointers.

The physical tree follows those ownership boundaries instead of collecting
unrelated classes in a flat `Bindings` or `Hooks` directory:

```text
Client/
|-- Automation/
|   |-- AppearanceCycle/
|   |-- CharacterSnapshot/
|   |-- FixtureDocuments/
|   |   |-- Hooks/
|   |   `-- Native/
|   `-- Runtime/
|-- Core/
|   |-- Capabilities/
|   |-- Diagnostics/
|   `-- GameThread/
|       |-- Hooks/
|       `-- Native/
|-- Game/
|   |-- Creature/
|   |   |-- Bindings/
|   |   |-- Control/
|   |   |-- Hooks/
|   |   |-- Locomotion/
|   |   |   |-- Bindings/
|   |   |   |-- Hooks/
|   |   |   `-- Native/
|   |   `-- Native/
|   |-- Entity/Bindings/
|   |-- HeroPawn/
|   |   |-- Bindings/
|   |   `-- TransformProbe/
|   |       |-- Hooks/
|   |       `-- Native/
|   |-- NPC/Bindings/
|   |-- Player/Bindings/
|   |-- Quest/Bindings/
|   `-- World/Bindings/
|-- Scripting/
|   |-- Bindings/Registry/
|   `-- Runtime/
|       |-- Capabilities/
|       |-- Events/
|       |-- Host/
|       |-- Scheduling/
|       `-- Storage/
`-- UI/
    |-- FrontEnd/
    |   |-- Hooks/
    |   `-- Native/
    |-- Hud/Bindings/
    `-- MainWindow/
```

Bindings live beside the service they expose. The only centralized binding
folder is the small registry that composes those domain modules. A hook follows
the same rule: its native signature and validation are defined first under the
owning domain, while installation/state live in a separate hook class in that
domain. `Main.cpp` is only allowed to compose lifecycle objects; the remaining
historic automation and probe code there is being extracted incrementally.

## Runtime contract

Every `.as` file below `bin/Release/scripts` is an independent module. One bad
module does not prevent valid modules from compiling. Supported callbacks are:

```angelscript
void OnLoad();
void OnUnload();
void OnTick(float deltaSeconds);
void OnKeyPressed(uint virtualKey, bool shiftPressed);
void OnWorldReady();
```

`OnStart` remains an alias for `OnLoad`. F5 discards and recompiles deployed
modules. Failed callbacks disable only their owning module. The runtime also
provides named events and cancellable timers:

```angelscript
void HandleWorldReady(const string &in name, const string &in detail) { }
void DelayedWork() { }

uint subscription = Events::Subscribe("WorldReady", @HandleWorldReady);
uint once = Scheduler::After(0.5f, @DelayedWork);
uint repeating = Scheduler::Every(1.0f, @DelayedWork);
Events::Emit("MyMod.CustomEvent", "optional detail");
Events::Unsubscribe(subscription);
Scheduler::Cancel(repeating);
Storage::SetString("server.character_id", "example-id");
string characterId = Storage::GetString("server.character_id", "");
```

Subscriptions and scheduled function handles are released before module
discard, so F5 cannot leave callbacks into unloaded bytecode. `Storage` exposes
string, signed 64-bit integer, double, boolean, existence, removal, and explicit
flush operations. Each module receives a separate file under `script-data`,
derived from its stable relative path rather than load order. Later framework
slices will add typed entity, combat, inventory, quest, UI, and network events;
game logic should not have to poll every engine state from `OnTick`.

Native bindings are fail-closed and report one of three capability states:

- `Verified`: current executable ABI validated and exercised by automation.
- `Experimental`: mapped and guarded, but its behavioral proof is incomplete.
- `Unavailable`: intentionally not callable until the current-build ABI is
  mapped.

Scripts can query `Capabilities::IsAvailable`, `IsVerified`, `GetStatus`, and
`Describe` before enabling optional behavior.

## Current coverage

| Domain | Verified now | Experimental now | Next parity slice |
| --- | --- | --- | --- |
| Runtime | recursive modules, isolation, callbacks, named event bus, cancellable scheduler, typed cross-launch module persistence | reload during active native control | coroutine-style waits and richer typed native events |
| World | Hero/script-name lookup, creature creation | - | entity enumeration, region/map lifecycle, effects and objects |
| Entity | validity, life state, position/facing, teleport, core flags, name/definition/data/map metadata, interaction state | immediate attack, metadata/interaction mutation | interaction messages, door actions, attachments, stable network identity |
| Creature | scripted-control acquisition, move/follow, combat-health reads, Hero-frame-to-NPC native navigator routing, movement-facing ownership, friendly decision policy, hidden-Hero shadow follow | health writes, animation/combat calls, resolved player ATTACK-to-NPC native ability routing | broader factions/perception policy, weapon selection, combo/flourish/block, ranged combat, spells |
| Player | Hero service and combat-health reads | combat-health writes, resolved ATTACK command observation without raw mouse polling | typed gold/will/experience snapshot, broader input routing, combat state, death/jail transition |
| HeroPawn | authoritative Hero retained during scripted puppet presentation | drawable suppression | typed morph/age snapshot, equipment/hair/tattoo and NPC masquerade presentation |
| UI | - | screen message | Scaleform discovery, nameplates, inventory/trade/custom menus |
| Quest | registered/active/completed/failed state reads | - | guarded mutations, objectives, rewards, timers, daily quest adapter |
| Inventory/economy | - | - | items, gold, containers, keys, prices, transfers |
| Property/doors | - | - | ownership, lock state, key/allowlist authorization, shop stock |
| Audio | - | - | sound/conversation API, then proximity voice integration |

The ordinary debug module at `scripts/debug/appearance_cycle.as` is a consumer
of this public surface. It does not call private probe hooks. It resolves the
Hero through `Player`, creates creatures through `World`, validates combat
health through `Creature`, acquires native scripted control, clears its action
queue, and routes verified Hero frame displacement into the first guard's
native navigator. The guard's own slot-22 frame update then produces its motion
fields, physical displacement, retail locomotion evaluation, and animation
state activity before restoration. This does not teleport the NPC or replace
its movement stack. A facing delta is not yet attributed to player input
because the creature's look and perception systems remain independently active.

Run `20260811-105818-185-34352` is the current combined framework gate. It restored
state written by a prior process, passed typed storage round-trips/removal and
flush, delivered one-shot/repeating scheduled callbacks, delivered and removed
a named `WorldReady` subscription, read entity health/interaction/metadata,
distinguished active, registered, and completed quest state, and completed the
native player-frame routing/restoration cycle with no failure event.

## Promotion rule

A native operation becomes `Verified` only after all of the following hold:

1. The supported executable fingerprint and owning vtable or function prefix
   validate.
2. Invalid or stale handles fail without invoking the engine.
3. The call is made on an appropriate game-thread boundary.
4. An isolated adult-save automation exercises the real operation and records
   structured evidence.
5. The restore/teardown path completes without a crash or leaked live action.

This is deliberately stricter than copying FSE's historic slot numbers. Fable
Anniversary's current interface has non-uniform slot shifts, so blind parity
would create a wide but unsafe API.

The pinned `FableMenuAnniversary` reference confirms useful current-build
component IDs and layouts for Hero stats, morphs, experience, appearance, and
player acquisition. Those definitions are being treated as candidates for a
typed, read-only-first `Player`/`HeroPawn` snapshot API. See
`references/fable-menu-anniversary.md`; direct field mutation is not promoted
from a trainer reference without our normal executable validation and isolated
round-trip gate.

The August 10 quest-state mapping pass confirmed this rule concretely. FSE's
historic quest slots 276 onward initially resolve to combat-health and unrelated
state functions in the supported Anniversary executable. Current-build
insertions shift the verified quest predicates to slots 299-302. Those reads are
now public and automation-proven; adjacent mutations remain unavailable until
their semantics and save effects pass the same gate. FSE remains the coverage
checklist and semantic vocabulary.
