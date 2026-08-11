# FableTogether

Multiplayer mod and script-extender framework for Fable Anniversary.

## Project

- `FableTogether.Launcher.exe` starts the Win32 game suspended and injects the client DLL.
- `FableTogether.Client.dll` provides validated native hooks and the AngelScript runtime.
- The current multiplayer foundation includes local host/guest process isolation, menu/save automation, and NPC appearance, locomotion, facing, and combat routing.
- The supported development installation is recorded in `config/fable-installation.json`.

The launcher resolves Fable from an explicit `--exe` or `--game-dir`, an alongside deployment, or the development fallback at:

```text
D:\SteamLibrary\steamapps\common\Fable Anniversary
```

## Build and run

Build the Win32 Release solution in Visual Studio or with MSBuild:

```powershell
msbuild FableTogether.sln /p:Configuration=Release /p:Platform=Win32
.\bin\Release\FableTogether.Launcher.exe
```

All deployable files are written to the single `bin\Release` directory. Use `--dry-run` to validate paths without starting the game.

## Local multiplayer development

Run the automated two-process title-screen acceptance:

```powershell
.\bin\Release\FableTogether.Launcher.exe --dual-instance-test
```

This starts isolated `host` and `guest` instances in side-by-side 1280x720 windows. Each instance has separate logs, events, script storage, Documents/saves, and local peer identity. Ordinary launches retain the retail UE3 singleton behavior.

Start one isolated instance manually with:

```powershell
.\bin\Release\FableTogether.Launcher.exe --local-instance host
.\bin\Release\FableTogether.Launcher.exe --local-instance guest --local-session my-session
```

## Automation

Available scenarios:

```powershell
.\bin\Release\FableTogether.Launcher.exe --automation observe_frontend
.\bin\Release\FableTogether.Launcher.exe --automation observe_save_list
.\bin\Release\FableTogether.Launcher.exe --automation bootstrap_fixture_probe
.\bin\Release\FableTogether.Launcher.exe --automation load_fixture
.\bin\Release\FableTogether.Launcher.exe --automation appearance_cycle
```

Run artifacts are written below `bin\Release\artifacts`. Automation and local multiplayer modes use isolated Documents roots instead of ordinary Fable saves.

## Documentation

- [Scripting framework](docs/scripting-framework.md)
- [Local dual-instance development](docs/local-dual-instance.md)
- [Automated session roadmap](docs/automated-session-roadmap.md)
- [Initial hook analysis](docs/initial-hook-analysis.md)
- [Appearance proxy](docs/appearance-proxy.md)
- [Design ideas](docs/design-ideas.md)
