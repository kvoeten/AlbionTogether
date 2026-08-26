# Local dual-instance development

This mode exists only for same-machine multiplayer development. It does not
change an ordinary AlbionTogether launch and it does not replace or modify the
installed retail executable.

## Command

```powershell
.\bin\Release\AlbionTogether.Launcher.exe --dual-instance-test
```

Optional bounds:

```powershell
.\bin\Release\AlbionTogether.Launcher.exe --dual-instance-test --timeout 120 --hold 10
```

The host must reach an initialized title UI and responsive game window before
the guest is created. The guest must independently pass the same gate. Both
windows have a 1280x720 outer footprint and are placed at x=0 and x=1280.

## Isolation contract

The retail Win32 startup creates the named Windows mutex
`UnrealEngine3_8` and exits when `CreateMutexW` reports
`ERROR_ALREADY_EXISTS`. Named mutexes are shared between processes in the same
Windows session, unlike ordinary process-local singleton state.

For an explicit local instance, the injected client replaces only Fable's
validated `CreateMutexW` import before the suspended game main thread resumes.
Only an exact `UnrealEngine3_8` request is rewritten:

```text
Local\AlbionTogether.UnrealEngine3_8.<session-id>.host
Local\AlbionTogether.UnrealEngine3_8.<session-id>.guest
```

The hook is signature- and import-slot-validated against the supported retail
Win32 executable and fails closed. No rewrite is installed for an ordinary
launch.

Each role also receives distinct paths for:

- `client.log`;
- `events.jsonl`;
- redirected `Documents` and therefore Fable saves/configuration;
- AngelScript `script-data`.

The Documents import hook is installed in the same pre-main boundary, before
Fable can query the user's ordinary Documents directory. The dual test begins
with empty isolated Documents roots and never copies a real save into them.

## Identity and Steam

The development peer identity is the explicit local role (`host` or `guest`)
plus the local session ID. No Steam ID or persona lookup participates in that
identity. The launcher still supplies retail App ID 288470 to both child
processes so the installed game can use its expected Steam boot context. This
test therefore proves multiplayer identity independence from Steam, not a
Steam-free build of the retail game.

## Acceptance gate

The command returns success only when both children report all of the following:

- local session and role identity;
- installed and exercised UE3 mutex rewrite;
- pre-main Documents redirect;
- role-scoped script storage;
- initialized front-end title UI;
- installed client hooks;
- distinct live PIDs and HWNDs;
- responsive windows throughout the coexistence interval;
- run-scoped shutdown events for both exact children.

It refuses to start when any pre-existing `Fable Anniversary.exe` process is
present, preventing the test from confusing or closing a game it does not own.
