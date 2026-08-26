# Automated session bootstrap roadmap

## Goal

Build a repeatable Fable Anniversary integration test that launches and
injects the client, observes the front-end and save lifecycle, enters a known
map with a controlled character, verifies that the session is playable, and
shuts down with a machine-readable result. The completed path must not require
keyboard/mouse input, window focus, or a person choosing a save.

Unsafe in-place player transformation remains shelved. The existing probe and
its crash evidence remain available for analysis, but its hooks and compatibility
guards must not run in an automated-session build. Identity-preserving native
NPC proxies are now exercised as a separate, fail-closed automation scenario.

## Evidence and initial hook strategy

The executable's UE3 reflection data exposes a useful front-end lifecycle:

- `UUI_PageContinueGameexecDoBegin`
- `UUI_PageContinueGameexecDoInit`
- `UUI_PageContinueGameexecDoStartPlay`
- `UUI_PageContinueGameexecDoEnd`
- `UUI_PageContinueGameexecDoOnUIEvent`
- `UUI_PageContinueGameexecDoTick`

It also contains Fable-specific state names such as `new game`, `load game`,
`continue game`, `continue_game_hero`, and `UI_PageContinueGame`. At the engine
level, reflected `AWorldInfo` functions include `GetMapName`, `IsMenuLevel`,
`PrepareMapChange`, `IsMapChangeReady`, `CommitMapChange`, and
`SeamlessTravel`; `UEngine` exposes `PlayLoadMapMovie` and
`GetCurrentWorldInfo`.

## Current baseline (historical path through 2026-08-08)

The first autonomous gate is working:

- the default client is observation-only and the transformation experiment is
  available only through `--transform-probe`;
- every automation run has a unique ID and JSONL event transcript;
- startup movies are skipped for automation;
- `UI_MenuFrontEndStart` initialization is captured on the game thread;
- delayed, bounded Enter attempts advance the title screen through its normal
  input path;
- the main menu is confirmed semantically by its preferred vtable
  `0x02F302A4` and native `DoBegin` implementation `0x01FEFC00`;
- the preconstructed `UI_PageLoadGame` object is identified by vtable
  `0x02F30D84`, then its validated native `DoBegin` (`0x01FF4F60`) and `DoTick`
  (`0x01FF6510`) methods are invoked on the game thread without selecting a
  save or calling `DoStartPlay`;
- `observe_save_list` reached native list readiness on its first semantic tick
  and retained `selection=0xFFFFFFFF` (no selected save);
- automation validates and redirects Fable's imported `SHGetFolderPathW`
  Documents lookup to a launcher-owned fixture root before the game starts;
- run `20260807-183553-098-20668` created only an empty isolated Fable profile
  structure, passed the save-list gate, and left the size/timestamp metadata of
  every ordinary save file unchanged;
- launcher and client share a run-scoped shutdown event, and only the process
  created for that run is exited;
- run `20260807-180512-319-30484` completed the full gate with exit code `0`.
- run `20260807-182937-383-40352` completed the semantic save-list gate with
  exit code `0` and no Fable process left behind.
- run `20260807-184430-637-40884` created a fresh isolated Hero1, entered the
  Oakvale childhood world, resolved `SCRIPT_NAME_HERO`, and shut down with exit
  code `0` without user input;
- native save-list enumeration is now mapped as a contiguous 18-record array
  with a 0x2C-byte stride and stable identity at +0x00. In the retained fixture,
  identity 0 is valid `AutoSave`, identity 1 is valid `QuestCheckPoint`, and
  identities 2-17 are invalid empty slots;
- Fable's default Continue selection preferred `QuestCheckPoint` identity 1.
  The loader therefore requires one valid exact `AutoSave` name and explicitly
  places identity 0 in `UUI_FrontEndMainMenu +0x78` before invoking Continue;
- `load_fixture` copies its explicitly supplied isolated source into a new
  run-specific working Documents tree, and rejects sources within the ordinary
  user Documents tree;
- run `20260807-190206-265-41976` completed exact AutoSave enumeration,
  main-menu Continue, front-end retirement, game-script readiness, Hero/world
  readiness, and clean shutdown with exit code `0`. A before/after SHA-256
  comparison confirmed the retained source fixture was unchanged.
- run `20260807-192517-883-19664` identified the active object as
  `CThingPlayerCreature`, sampled its transform and both health systems for
  three ticks, and confirmed actual combat HP at `20 / 20`. Fable's separate
  progression component named `Health` was `0 / 2000`;
- `--character-snapshot` now copies a strict JSON snapshot into each immutable
  artifact bundle. The client waits for three stable baseline samples before
  applying it through native `TeleportThing` and `CThingPlayerCreature` combat
  health mutation paths;
- run `20260807-193935-836-24140` moved the Hero from X `3494.918457` to the
  snapshot X `3509.918457`, held the target ground position and facing, changed
  combat HP from `20` to `17`, verified three consecutive samples, preserved
  the retained fixture byte-for-byte, and shut down with exit code `0`;
- the preceding bounded run `20260807-193733-116-7664` proved that
  `TeleportThing` performs collision/ground correction: X was exact, while the
  supplied Z `15.806825` converged to `18.378922`. The harness correctly timed
  out instead of accepting the mismatched snapshot, after which the fixture was
  corrected to the engine-resolved ground coordinate.
- the loaded Hero's position resolves through Fable's native region manager to
  stable region index `4`. This identity is now mandatory in the snapshot and
  is asserted both before and after application; cross-region snapshots are
  rejected so travel cannot be smuggled through a local XYZ teleport;
- five final cold starts passed consecutively with no client failure, exactly
  three post-snapshot samples, clean shutdown, an identical snapshot hash, and
  an unchanged retained save source: `20260807-194748-739-25280`,
  `20260807-194829-312-38088`, `20260807-194908-050-9516`,
  `20260807-194949-578-23624`, and `20260807-195030-424-34860`. Final deployed
  build smoke run `20260807-195240-852-7012` also passed after adding the
  region-resolver signature gate.
- the retained New Game fixture was replaced for appearance testing by a
  disposable adult Hero in Bowerstone North (region index `32`, position
  `3260.375488, 4281.842285, 20.5`, combat health `130 / 130`, progression
  `931 / 2000`). The build deploys it as `fixtures/adult-town`, and fixture-load
  automation uses it by default without reading the ordinary Documents tree;
- the appearance path now keeps the real `CThingPlayerCreature` authoritative,
  hides it through the validated `.Drawable` seam, and presents generic creature
  proxies with attack, damage, and collision disabled. Inactive forms are hidden
  and cached because direct native destruction is not compatible with retaining
  the conversion source Hero;
- `appearance_cycle` proves guard, Bowerstone villager, hobbe, cached guard
  reuse, Hero restoration, and a five-second soak. Three consecutive cold runs
  passed: `20260807-231030-719-34008`, `20260807-231125-573-34116`, and
  `20260807-231215-043-36104`;
- runtime component type `0x2` resolves to `CTCPhysicsControlled` on the Hero and
  `CTCPhysicsNavigator` on a native NPC. Vtable slot 32 accepts the same
  `float[3]` movement-vector shape on both. The minimized hook preserves the
  Hero call and forwards the vector to the current NPC navigator;
- run `20260807-235632-761-44712` forwarded 15 vectors and measured 3.209 world
  units of native guard displacement before completing villager, hobbe, cached
  guard, Hero restore, fault-free soak, and clean shutdown;
- the proxy is a complete `CThingAICreature`; hidden cached forms therefore
  retained live AI and could attack while invisible. The client now validates
  `CThingAICreature + 0x1E0 -> CAIBrain`, hooks only `CAIBrain` vtable slot 4,
  and suppresses updates only for registered active/cached proxy brain pointers;
- run `20260808-105155-995-46800` proved selective puppet mode across guard,
  villager, hobbe, and cached guard reuse. It suppressed 58, 45, 41, and 45
  active-form brain updates respectively; every form began with zero active or
  queued scripted actions, native movement still passed, and shutdown was clean;
- a prologue-validated observer at `CThingCreature` construction now performs
  only lock-free accounting inside the native hook. Run
  `20260808-000928-546-25080` observed 22 ordinary generic creatures constructed
  before the appearance proxy, all on population worker thread `41404` rather
  than game thread `14576`. It also observed an immediate generic-creature
  retirement on the worker thread, then passed movement (15 vectors, 2.908
  units), the full appearance cycle, protected restore soak, ordinary Hero flag
  restoration, and clean shutdown.

This baseline is autonomous but temporarily brings the game window to the
foreground for the title-screen input. That remaining focus dependency is
limited to the title gate. Save-list entry no longer depends on menu input;
fixture selection and map loading now use semantic game handlers as well.

## Current framework baseline (2026-08-10)

The cached-proxy and movement-vector forwarding experiments above are retained
as discovery history, not the active implementation. The current default path
uses a normal AngelScript module, creates a fresh native creature, acquires its
retail scripted-control object, and submits `Follow` so the creature's own
navigator owns movement and rotation. There is no default CAIBrain suppression
or movement-vector forwarding hook.

Run `20260810-121428-259-19448` passed the adult-town appearance cycle with the
general scripting API. It additionally verified entity definition/name/map
metadata, typed state restored from an earlier process, the named event bus,
repeating and one-shot scheduling, cancellation and unsubscription from inside
callbacks, current quest registration/active/completed/failed state, native
interaction state, health, movement/facing, Hero restoration, and clean
run-scoped shutdown.

The API target is FSE-scale, but the historic FSE vtable is not ABI truth for
this executable. A quest mapping audit found unrelated combat-health functions
at the old FSE quest slot range before current-build insertions were aligned;
the verified quest predicates are slots 299-302 here. Every promoted native
call must still be mapped against the supported binary and exercised by this
isolated workflow.

The first implementation should therefore observe UE3 object/function event
dispatch and record only a filtered set of UI, save, world, and Hero events.
Specific lower-level hooks should be added only after the observation log shows
which native or script path the retail game actually uses. Automation commands
will be queued and drained from a verified game-thread hook; window messages
and the existing window timer may wake the client, but must not directly call
game functionality.

This instrumentation should include actor/world setup and teardown around a
normal map transition. That gives us the component registration, destruction
order, and presentation-reference lifetime baseline needed before reconsidering
`TurnCreatureInto`.

## Safety boundary

- Never select, overwrite, delete, or migrate an ordinary player save.
- Discovery begins in observation-only mode with no automatic selection.
- Mutating tests require a dedicated AlbionTogether test profile/save root or an
  explicitly identified disposable fixture. Automation defaults to
  `bin\Release\fixtures\automation\Documents` and validates the redirect before
  any semantic save or New Game action is allowed.
- Do not suppress native faults to make an automated run appear successful.
- Validate the analyzed executable fingerprint and every installed native hook.
- A timeout, unexpected dialog, crash, dump, or unrecognized UI state is a
  failed run with retained diagnostics.
- The launcher owns and terminates only the process that it created for the
  current run.

## Work plan

### 1. Separate the probes

Refactor the injected client into independently enabled facilities:

- startup and structured diagnostics;
- UE3 lifecycle observation;
- automation command queue;
- transformation experiment, disabled by default.

Keep one canonical deployment directory at `bin\Release`. Add an automation
scenario argument without introducing versioned output folders.

**Gate:** a normal launch retains startup logging but installs no
transformation hotkey or crash-compatibility guard unless that experiment is
explicitly requested.

### 2. Map the retail lifecycle

Use IDA and a filtered runtime observer to identify:

1. engine/game-thread tick or event dispatch;
2. title/front-end creation and active-page changes;
3. continue/load page initialization and save-list population;
4. the exact selected-save-to-start-play call chain;
5. map load begin, world replacement, streaming completion, and playable-world
   readiness;
6. Hero creation, possession, post-load initialization, and teardown;
7. actor/component registration and teardown during ordinary travel.

Logs must contain timestamps, thread IDs, object class/name, function name,
world/map identity, and a correlation/run ID. High-frequency calls are filtered
or sampled so a run remains readable.

**Gate:** one manually initiated disposable-save run produces a deterministic
lifecycle trace with stable candidate hooks and no behavioral changes. This is
the final manual capture expected for the project; subsequent milestones use
the harness.

### 3. Add a launcher-to-client test protocol

Add a small local control channel (prefer a named pipe) and explicit states:

`ProcessStarted -> ClientReady -> FrontendReady -> SaveListReady ->`
`StartRequested -> MapLoadStarted -> WorldReady -> HeroReady ->`
`AssertionPassed/AssertionFailed -> ShutdownComplete`

The launcher supplies a run ID, scenario, timeout, and fixture path. The client
reports structured events; the launcher writes a JSONL transcript, watches the
child process, collects the client log and crash-dump paths, and returns a
nonzero exit code on failure. Human console output is a summary of the same
state machine.

**Gate:** a no-op `observe_frontend` scenario launches the game, detects the
front end without pixel matching, records its state, requests run-scoped
shutdown, and returns a reliable exit code. **Passed on 2026-08-07**, with the
temporary title-screen focus dependency described above.

The read-only extension `observe_save_list` also passed on 2026-08-07. It
exercises the native Load Game page lifecycle directly, proves list readiness,
and asserts that no save entry was selected before run-scoped shutdown.

The launcher-client control path currently uses a run-scoped shutdown event and
append-only JSONL rather than a named pipe. That smaller protocol already
provides correlated states, bounded timeouts, exact-PID ownership, and reliable
pass/fail behavior; a duplex pipe remains optional when live commands require
it.

### 4. Automate an isolated native-save session

Discover and invoke the same handler used by the continue/load page, on the
game thread, against a dedicated test fixture. Do not synthesize keyboard or
mouse input. Prefer semantic selection by stable save identity; never rely on
slot order or "most recent" state.

After loading, assert at minimum:

- expected map/world identity;
- a valid local player/controller and possessed Hero;
- finite Hero position and health;
- several consecutive playable game ticks;
- clean map/Hero teardown during requested shutdown.

**Gate:** `AlbionTogether.Launcher.exe --automation load_fixture` completes from
a cold start without user interaction and produces a self-contained result
bundle. **Passed on 2026-08-07** in run `20260807-190206-265-41976`; repeat-run
reliability and deeper in-world assertions remain part of step 6.

### 5. Establish the server-character bootstrap seam

Treat the game's save as engine bootstrap state, not multiplayer authority.
Determine which of these paths is safest from the lifecycle evidence:

1. load an isolated minimal AlbionTogether bootstrap save, then apply a server
   character snapshot after Hero/world readiness; or
2. invoke the new-game/world bootstrap directly and supply the required Hero
   state without deserializing a normal save.

The fixture schema should initially contain server character ID, display name,
target map and spawn transform, health, age/morph state, appearance/equipment,
and the minimum progression flags needed for unrestricted world traversal.
Inventory, ownership, quests, and economy remain server-authoritative later.

**Gate:** changing only the fixture selects a different character snapshot and
spawn location while ordinary Steam saves remain byte-for-byte untouched.

**Passed on 2026-08-07** in run `20260807-193935-836-24140`. The implemented
schema currently covers identity/display metadata, the exact AutoSave bootstrap
identity, engine region, spawn transform, and current combat HP. Appearance, maximum health,
inventory, age/morph state, and unrestricted-world progression remain later
schema slices.

### 6. Make the run self-verifying

Add harmless in-world probes performed through engine hooks: read position,
request a small controlled movement or rotation, observe the resulting Hero
state, and verify world ticks continue. Capture a screenshot or D3D frame as a
diagnostic artifact, but do not make pixel matching the primary correctness
signal.

Run at least five cold-start repetitions and fail on a stale process, dialog,
timeout, crash dump, unexpected map, missing Hero, or dirty teardown.

**Completion gate:** one command repeatedly reaches the controlled playable
session, verifies it, and exits cleanly with no human involvement. The retained
artifacts are sufficient to diagnose any failed state transition.

**Passed on 2026-08-07.** The five consecutive cold runs listed in the current
baseline all met this gate. A D3D screenshot remains a useful later diagnostic,
but correctness is based on semantic save identity, region, native Hero type,
transform, combat HP, consecutive samples, structured failure states, and
owned-process shutdown rather than pixels.

## First implementation slice (completed)

The first slice is deliberately observation-only:

1. disable transformation facilities in the default client mode;
2. add run/scenario configuration and structured JSONL logging;
3. locate and signature-validate UE3 event/tick dispatch;
4. log filtered front-end, save, world, Hero, actor-setup, and actor-teardown
   events;
5. add `observe_frontend` with timeout and graceful process shutdown.

The observation slice is complete. The harness now creates and loads a
launcher-owned fixture, waits for a stable post-load Hero boundary, and applies
the first server-character snapshot fields with post-mutation assertions.

## Next multiplayer lifecycle slice

The next slice turns the appearance proxy experiment into a server-entity
lifecycle rather than treating it as a keyboard-driven costume list:

1. Assign every networked player/NPC a stable server entity ID independent of
   native pointers, save identities, and region-load generations.
2. Capture the normal Fable creature creation, component registration, region
   insertion, retirement, and teardown order. Construction/destruction can occur
   on a population worker, so hooks only enqueue immutable observations; game
   mutations and reconciliation drain at a verified simulation boundary.
3. At region readiness, reconcile a server-provided desired entity set against
   the locally bound set. Suppress or retire only network-managed vanilla NPCs;
   do not blanket-block scenery, doors, ambient systems, or engine actors.
4. Spawn missing server entities through the validated generic-creature path,
   bind ID plus generation to the resulting native creature, and retire bindings
   absent from the next authoritative region snapshot.
5. Use native `CTCPhysicsNavigator` as the locomotion owner. Local masquerade
   input comes from the hidden Hero; remote players and server NPCs consume
   server movement intent plus transform correction through the same adapter.
6. Move hidden-Hero shadowing to a simulation-update boundary, then add action,
   attack, ability, collision, and damage routing as separate verified layers.

**Gate:** one autonomous run loads the adult-town fixture, reconciles a small
server entity set, proves no unmanaged network NPC is present, moves one native
NPC through navigator intent, removes it on the next revision, survives a region
reload without stale bindings, restores the Hero, and exits without a fault.
