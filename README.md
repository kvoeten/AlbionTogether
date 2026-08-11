# FableTogether

Multiplayer mod for Fable Anniversary.

## Development launcher

`FableTogether.sln` contains a Win32 console launcher and the injected client
DLL. The launcher starts the game suspended, injects `FableTogether.Client.dll`,
and resumes the game only after `LoadLibraryW` succeeds.

Path lookup is designed for both deployment and local development:

1. `--exe` or `--game-dir` command-line override.
2. `Fable Anniversary.exe` beside the launcher.
3. A standard `Binaries\Win32` directory below the launcher.
4. The checked development install at
   `D:\SteamLibrary\steamapps\common\Fable Anniversary`.

The DLL is expected beside `FableTogether.Launcher.exe`. Every Visual Studio
configuration deploys both projects into the single canonical `bin\Release`
folder, so the launch command never changes and no files need to be copied into
the Steam installation during development. Do not create version-suffixed
deployment folders.

Build and inspect the resolved paths without starting the game:

```powershell
msbuild FableTogether.sln /p:Configuration=Release /p:Platform=Win32
.\bin\Release\FableTogether.Launcher.exe --dry-run
```

Run with an alternate installation:

```powershell
.\bin\Release\FableTogether.Launcher.exe --game-dir "D:\Games\Fable Anniversary"
```

Arguments after `--` are forwarded to the game.

## Client and scripting framework

A normal launcher run loads the AngelScript gameplay framework. Every `.as`
file below `bin\Release\scripts` is compiled as an independent module, and F5
reloads the deployed modules. The framework exposes typed `World`, `Entity`,
`Creature`, `Player`, `UI`, and capability APIs; FSE-scale quest, inventory,
property, progression, audio, and custom-UI coverage is the target rather than
a narrow multiplayer-only surface. See
[`docs/scripting-framework.md`](docs/scripting-framework.md).

Scripts can coordinate through `Events`, schedule cancellable callbacks through
`Scheduler`, and store typed per-module state through `Storage`. Persistent data
is kept in `bin\Release\script-data`, keyed by the module's stable relative
script path; scripts do not receive arbitrary filesystem access.

The first current-build quest slice is also available through `Quest`:
registered, active, completed, and failed state can be queried safely. Quest
mutation stays intentionally unavailable until its save effects are verified in
the disposable adult-save workflow.

The bundled debug module maps number-row `1` to an ordinary scripted creature
puppet cycle and `Shift+1` to restoration. It keeps the authoritative
`CThingPlayerCreature` alive, hides only its presentation, creates a full native
creature, and gives that creature Fable's scripted navigation control. The same
public API is available to other modules; the debug cycle has no private proxy
hook fallback.

Each launch receives a run ID and writes structured events to
`bin\Release\artifacts\<run-id>\events.jsonl`.

The old number-row `1` transformation experiment is available only through an
explicit unsafe opt-in:

```powershell
.\bin\Release\FableTogether.Launcher.exe --transform-probe
```

Use a disposable save and see `docs/transformation-probe.md` before enabling
it. The launcher supplies Steam App ID `288470` to the child process so direct
development launches retain the expected Steam context.

Automation scenarios use the same canonical deployment:

```powershell
.\bin\Release\FableTogether.Launcher.exe --automation observe_frontend
.\bin\Release\FableTogether.Launcher.exe --automation observe_save_list
.\bin\Release\FableTogether.Launcher.exe --automation bootstrap_fixture_probe
.\bin\Release\FableTogether.Launcher.exe --automation load_fixture
.\bin\Release\FableTogether.Launcher.exe --automation appearance_cycle
.\bin\Release\FableTogether.Launcher.exe --automation load_fixture --fixture-documents "D:\path\to\isolated\Documents" --character-snapshot .\config\server-character.fixture.json
```

`observe_frontend` is an autonomous smoke test. It skips startup movies, waits
for Fable's title UI to finish initializing, advances through the normal title
input path, verifies the retail main-menu `DoBegin` implementation, and shuts
down the exact process created for the run. A successful run returns `0`; a
hook mismatch, early process exit, missing state, or timeout returns nonzero.

`observe_save_list` continues from that gate without synthesizing menu input. It
captures Fable's initialized `UI_PageLoadGame` object, validates and invokes its
native `DoBegin`/`DoTick` lifecycle on the game thread, verifies that the list
is ready with no selected entry, and shuts down. It never calls `StartPlay`.
Automation redirects Fable's Documents lookup to
`bin\Release\fixtures\automation\Documents` by default, so this path cannot
enumerate or alter the user's ordinary `Documents\My Games\FableHD` saves. Use
`--fixture-documents <dir>` to select another dedicated automation root.

`bootstrap_fixture_probe` creates a new run-specific Documents root, starts a
New Game through the validated main-menu event handler, waits until
`SCRIPT_NAME_HERO` resolves in the playable world, and shuts down. Its resulting
Hero1 profile is a disposable fixture for later tests.

`load_fixture` defaults to the bundled disposable adult-Hero fixture in
Bowerstone North. `--fixture-documents` can override it with another isolated
source containing `Hero1\Profile.bin` and `Hero1\AutoSave`. The launcher rejects
any source below the ordinary user Documents tree and copies the source into a
fresh run-specific working directory before starting Fable. The client enumerates Fable's native
0x2C-byte save records, requires one valid exact `AutoSave` name, overrides the
main menu's Continue identity, drives the real Continue state machine, and
passes only after the loaded world's Hero and active `CThingPlayerCreature`
produce three stable transform, combat-health, and progression samples. It does
not rely on list position or Fable's default most-recent selection.

`--character-snapshot <json>` adds the first server-character bootstrap layer.
The launcher copies the JSON into the run artifacts before launch. After the
AutoSave load settles for three samples, the client validates the Hero type,
applies the snapshot through Fable's retail `TeleportThing` and combat-health
paths, and requires three exact post-application samples. The current schema
contains a server character ID, display name, exact `AutoSave` bootstrap
identity, expected engine region, spawn transform, and current combat HP.
Cross-region snapshots are rejected; region travel remains a separate map-load
operation. Maximum health, appearance, inventory, and progression mutation are
intentionally not enabled yet.

`appearance_cycle` loads that same mock save, requires the adult Bowerstone
state (`region 32`, full health, nonzero progression), and exercises guard,
villager, hobbe, and Hero restoration entirely through the public AngelScript
services. It validates both Hero and spawned-creature combat-health reads, then
holds forward input and routes the Hero's signature-validated frame
displacement into the first guard's native physics navigator. The guard's own
`CThingCreature` frame update must then produce physical displacement,
locomotion-mode input, and animation-state activity. This is not a per-frame
teleport. Visual testing has confirmed native gait and player-owned facing for the
supported NPC proxy forms; the automated gate additionally verifies locomotion,
shadow-follow, hostility policy, and NPC combat-router lifecycle events.
The combat gate submits an automation-only window ATTACK stimulus, observes
Fable's resolved player ability command (`0x16`), and verifies that the native
`CThingCreature` ability call substitutes the controlled NPC for the hidden
Hero. The injected client does not inspect mouse state or invent a combat
target; the NPC retains its own attack animation, weapon sweep, hit detection,
and targeting stack.
It passes only while the real Hero pointer,
vtable, region, and combat health remain stable through the restore soak. The
fixture and debug module deploy beside the launcher in the normal build.

The title screen currently needs the game window to be foreground while a
bounded synthetic Enter transition is submitted. Everything after that gate—
save enumeration, exact selection, New Game, Continue, and world readiness—uses
semantic UE3/Fable handlers against launcher-owned fixture roots.

The current local installation reference and executable fingerprint are kept in
`config/fable-installation.json`. See `docs/initial-hook-analysis.md` for the
reverse-engineering evidence and `docs/design-ideas.md` for the living RP/gameplay
design. The active menu/save/map automation goal and its safety gates are in
`docs/automated-session-roadmap.md`.
