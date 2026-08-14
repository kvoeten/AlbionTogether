# FableTogether

Early alpha multiplayer for Fable Anniversary.

![Two players exploring Albion together](media/MovementReplication.gif)

## What works

- Load your own character and see other players as their characters.
- Walk around Albion independently and meet up anywhere.
- Hero appearance is synchronized.
- Movement, turning, and walking animations are synchronized smoothly.

## Not yet

- Hero weapons and equipment changes.
- Combat.
- NPC position synchronization.
- Shared cutscenes and quest progress.

## Setup

FableTogether currently targets the 32-bit Steam version of Fable Anniversary.

1. Download the latest release and extract it into Fable Anniversary's `Binaries\Win32` folder.
2. Start the host:

   ```powershell
   .\FableTogether.Launcher.exe --host --player-id Host
   ```

3. On another computer, join using the host's IPv4 address:

   ```powershell
   .\FableTogether.Launcher.exe --join 192.168.1.10 --player-id Guest
   ```

4. Each player selects the save and character they want to use.

The default UDP port is `38171`. Use `--port <port>` on both computers to change it.

This is an early alpha. Back up your saves before testing.

## Build

Build the Win32 Release solution in Visual Studio or with MSBuild:

```powershell
msbuild FableTogether.sln /p:Configuration=Release /p:Platform=Win32
```

Build output is written to `bin\Release`.

## Documentation

- [Scripting framework](docs/scripting-framework.md)
- [Local multiplayer development](docs/local-dual-instance.md)
- [Native hook analysis](docs/initial-hook-analysis.md)
- [Design ideas](docs/design-ideas.md)
