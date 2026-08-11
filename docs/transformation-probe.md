# Single-player transformation probe

> This document describes the explicitly unsafe `--transform-probe` route.
> Normal launches now use the identity-preserving visual proxy described in
> [appearance-proxy.md](appearance-proxy.md).

This experiment exercises Fable's own creature-replacement path before any
multiplayer work depends on it. Anniversary's `GetHero` wrapper requires an
active game-script execution frame and writes through frame-owned storage. The
probe therefore uses the context-free `GetThingWithScriptName` implementation to
resolve `SCRIPT_NAME_HERO` into an explicit `CScriptThing` result buffer, then
calls Anniversary's `TurnCreatureInto` implementation on the game/window thread.

## Controls

- Press the number-row **1** key once to advance to the next creature form.
- Press **Shift+1** to attempt an immediate return to `CREATURE_HERO`.
- Holding the key does not repeat; each cycle requires a fresh key press.
- The probe consumes the `1` key while active, even in game menus.

The cycle is deliberately human-first, with less compatible skeletons near the
end:

1. `CREATURE_HERO` (recovery form; the first normal press advances past it)
2. `CREATURE_BS_GUARD`
3. `CREATURE_BS_GUARD_CROSSBOW`
4. `CREATURE_PRISON_GUARD`
5. `CREATURE_KN_GUARD`
6. `CREATURE_BS_VILLAGER_MALE`
7. `CREATURE_BS_VILLAGER_FEMALE`
8. `CREATURE_TRADER_01`
9. `CREATURE_BANDIT_GRUNT`
10. `CREATURE_RIVAL_HERO_WHISPER`
11. `CREATURE_RIVAL_HERO_THUNDER`
12. `CREATURE_HOBBE_GRUNT`
13. `CREATURE_BALVERINE_EASY`
14. `CREATURE_HERO_CHILD`

## Test procedure

Use a disposable save. Creature replacement can invalidate assumptions in hero
animations, equipment, combat, camera, quest, or save code even when the visual
swap succeeds.

1. Build and run `bin\Release\FableTogether.Launcher.exe`.
2. Load the disposable save and stand in a quiet exterior region, out of combat
   and outside a cutscene or menu.
3. Press `1` once. Confirm the Bowerstone guard appearance, movement, camera,
   interaction, weapon, and combat behavior.
4. Continue through human forms before trying the Hobbe or Balverine stress
   cases. After each form, note whether movement, targeting, expressions,
   equipment, attacks, doors, and region transitions remain functional.
5. Use `Shift+1` to restore the hero. Do not save over a valued character after
   testing a form.

The launcher prints the exact run-scoped `client.log` path before starting the
game. The client writes it beside that run's structured event transcript below
`bin\Release\artifacts`. Each press records the requested definition and whether
the engine accepted it. If pressing
`1` appears to do nothing, check for `game interface is not ready`, `hero is
unavailable`, `engine rejected`, or `structured exception` in that file. A native
fault now includes its exact stage, exception code, instruction address, game
RVA, module, and (for access violations) the attempted operation and address.

The same diagnostics are streamed to a console while Fable is running. Startup
entries show the Steam context, executable validation, discovered game window,
window-procedure hook, polling timer, first timer event, Steam API load, and
game-script interface readiness. A press produces `direct hero lookup` details
before the transformation result, including the returned buffer, vtable,
implementation, and counted-pointer metadata. This makes input, hero resolution,
and the actual transformation separately visible stages.

The client also observes up to eight low-address access violations process-wide
and records the faulting module, address, thread, access type, and x86 registers
before allowing normal Windows exception dispatch to continue. This does not
prevent a crash; it preserves evidence for failures that happen later on another
game or rendering thread after a native call returned successfully.

Fable reads gameplay keys through its input layer, so the probe uses two paths:
it consumes ordinary `WM_KEYDOWN` messages when available and also polls the
physical number-row `1` state from a timer dispatched on the game-window thread.
Both paths queue the transformation; the native lookup and replacement calls run
from the timer rather than re-entering game code from inside keyboard-message
handling. The lookup creates a temporary counted hero handle, and the probe
currently retains both that handle and the replacement result for the process
lifetime. This mirrors the extender reference's persistent ownership and ruled
out immediate handle release as the guard crash's cause. The diagnostic is
capped at 64 handles (32 key presses); restart the game after reaching that
limit.

## Current guard crash evidence

The first direct `CREATURE_BS_GUARD` test (PID 35992 on 2026-08-06) proved that
input dispatch, `SCRIPT_NAME_HERO` lookup, and `TurnCreatureInto` all completed:
the engine returned correctly formed, non-null `CScriptThing` handles and the
client logged the transformation as successful. Roughly four seconds later a
different thread read address zero in `msvcrt.dll`, called through `d3d9.dll`.
Its first game return address maps to preferred VA `0x015D271C`, immediately
after a render object's virtual call at `0x015D271A`. This makes the next
investigation a post-transformation rendering/appearance-state problem, not an
input failure or an exception raised by the transformation call itself.

The second test (PID 29256) retained both handles with valid reference counts.
Only 12 milliseconds after the engine reported success, a worker thread entered
the component-`0x68` cache update and called preferred VA `0x01DB91E0`. The
converted actor's component existed, but its required dependency pointer at
component offset `+0x70` was null; `0x01DB91E8` consequently tried to read
`[null+0x78]`. This rules out handle lifetime and identifies an asynchronous
component-initialization gap exposed by the creature replacement.

The current build installs signature-validated entry hooks on the two helpers
that consume that dependency (RVAs `0x019B91E0` and `0x019B8A90`). They preserve
the original calculation whenever `+0x70` is initialized and temporarily return
the neutral level/progress values `1` and `0` while it is null. Each fallback is
logged. These are narrow diagnostic compatibility guards, not yet proof that
every hero-only system accepts an NPC definition.

The third test (PID 30936) proved those two guards worked: both neutral
fallbacks ran and the original immediate component-`0x68` failure did not
recur. Five seconds later, however, the originating dump captured a different
Hero update at preferred VA `0x01CFAF3D`. It queried the transformed creature
for component type `0x11`. The guard correctly reported that the component was
absent, but retail code continued with an uninitialized stack local whose stale
value was a return address. The timer-state updater at `0x01DD94D0` then treated
that code address as an object and faulted while writing at `0x01DD950D`. The
subsequent `ntdll.dll` write to address `0x14` was a secondary worker-termination
failure, not the originating fault.

The current diagnostic build intercepts that exact signature-validated branch.
When component type `0x11` exists, it replays the original instructions and
continues unchanged. When it is absent, it logs the event and follows the
routine's existing early-cleanup path instead of allowing the uninitialized
local to reach later Hero-only work. This is one bounded experiment to assess
the old creature-replacement facility; it is not the proposed multiplayer
architecture.

For multiplayer, preserve the real Hero/player logic actor and replace only its
presentation: skeletal mesh, materials and morphs, animation graph/sets,
equipment attachments, and movement-style presentation. A visual proxy is the
fallback for skeletons that cannot safely inhabit the Hero presentation stack.
`TurnCreatureInto` remains useful for discovering NPC definitions and their
presentation dependencies, but full creature-definition replacement is now
considered too invasive to be the primary player-appearance mechanism.

The fourth test (PID 20604) reached both compatibility paths successfully, then
reproduced the original delayed presentation failure. The originating dump is
an execute access violation on a render worker at `0x015E21B5`, inside a
destructor that releases three captured asynchronous presentation references.
Its member at object offset `+0x34` was no longer a reference object at all: it
pointed into `nvd3dum.dll`, and its alleged vtable produced garbage call target
`0xD9005D74`. The stack also returned through the earlier render dispatch at
`0x015D271C`. The later main-thread read from address `0x1` occurred while the
game window and process were already being torn down.

This is the stopping criterion for Hero `TurnCreatureInto` compatibility
guards. Skipping the destructor call would conceal invalid ownership, leak
captured resources, and allow corrupted presentation state to propagate. No
additional fault suppression should be added for this route. The next `1`-key
experiment should keep `CREATURE_HERO` and apply only Hero-compatible
presentation profiles. The shipped definitions include complete guard-colour
sets and NPC-inspired Hero outfits for Whisper, Thunder, Maze, and Scythe, which
give us a safe first layer before investigating arbitrary skeletal-mesh proxies.

Do not treat this as final proof that full creature replacement is impossible.
Before closing that route, capture and compare normal actor setup and graceful
teardown during ordinary play against the `TurnCreatureInto` path. In
particular, trace presentation-reference ownership, render-command draining,
component unregister/destruction order, and any explicit pre/post conversion
steps used by a genuine game-script caller. The delayed destructor corruption
may indicate that the probe omitted a required lifecycle operation rather than
that creature replacement is inherently unsupportable. This investigation
should precede any further compatibility guards or another transformation test.

Windows retained the corresponding local dump as
`C:\Users\kaz_v\AppData\Local\CrashDumps\Fable Anniversary.exe.35992.dmp`.
The retained-handle worker crash is
`C:\Users\kaz_v\AppData\Local\CrashDumps\Fable Anniversary.exe.29256.dmp`.
The missing-component test dumps are
`C:\Users\kaz_v\AppData\Local\CrashDumps\Fable Anniversary.exe.30936.dmp`
and `Fable Anniversary.exe(1).30936.dmp` in the same directory.
The asynchronous presentation-lifetime dumps are
`C:\Users\kaz_v\AppData\Local\CrashDumps\Fable Anniversary.exe.20604.dmp`
and `Fable Anniversary.exe(1).20604.dmp` in the same directory.
The IDA database contains comments at `0x015D271C` and `TurnCreatureInto`
(`0x01C98200`), plus named/commented worker helpers at `0x01DB91E0`,
`0x01DB8A90`, and `0x01CF2566`. The third crash adds the Hero update branch at
`0x01CFAF3D` and timer-state updater at `0x01DD94D0`.

## Version and safety gates

The probe is enabled only for the analyzed 32-bit executable with timestamp
`0x545D058C`, image size `0x035D5000`, entry-point RVA `0x0236A782`, and matching
native method signatures. It additionally verifies the live game-script
interface vtable and the exact `GetHero`, `GetThingWithScriptName`, and
`TurnCreatureInto` slots before every transformation. A different executable
build fails closed instead of calling an unverified address.

The relevant Anniversary-native discoveries are:

- live game-script interface pointer slot: RVA `0x031BBC34`;
- `CCharString` literal constructor/destructor: RVAs `0x012B7800` / `0x012B75D0`;
- `GetHero`: vtable index 70, RVA `0x01889940`;
- `GetThingWithScriptName`: vtable index 78, RVA `0x0189DF10`;
- `TurnCreatureInto`: vtable index 100, RVA `0x01898200`;
- `CScriptThing` vtable: RVA `0x02A5CBF4`, with destructor RVA `0x0135C7A7`
  and `IsNull` RVA `0x0135B9E0`.

The six-slot shift from older Fable interfaces was verified against the
Anniversary vtable and calling conventions; using the older index 94 would call
an unrelated method.
