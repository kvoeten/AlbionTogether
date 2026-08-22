# FableTogether

Experimental multiplayer mod for Fable Anniversary.

![Two players exploring Albion together](media/MovementReplication.gif)

## Current alpha

Two players can load their own Heroes, explore Albion independently, and meet up anywhere. Remote Heroes now keep their appearance, weapons, combat animations, and most Will spell effects.

### Done

- Load your own save character and see each other.
- Walk around Albion independently and meet in any region.
- Hero appearance, clothing, movement, rotation, and locomotion.
- Equipped weapons, including drawing and returning them to the Hero's back.
- Remote melee attacks and most Will spell animations and effects.
- NPC presence, position, movement, and health.
- NPC ownership when players split across different regions.
- Player health synchronization during combat.
- Stable map transitions and background rendering.

### Next

- Enemy hit reactions and complete PvP damage/reactions.
- Ranged combat, projectiles, death, and revival.
- Slow Time and Raise Dead synchronization.
- Shared quests, cutscenes, warps, and world progress.
- Equipment, loot, trading, currency, shops, and property ownership.
- Player names, interaction menus, door/key permissions, and RP tools.
- Proximity voice chat.
- A friendly host/join launcher UI and production networking.

This is an early development release. Back up ordinary saves before testing.

## Install and play

FableTogether currently targets the 32-bit Steam build of Fable Anniversary.

1. Download and extract the release into Fable Anniversary's `Binaries\Win32` folder.
2. Allow the chosen UDP port through the host's firewall. The default is `38171`.
3. Start the host:

   ```powershell
   .\FableTogether.Launcher.exe --host --player-id Host
   ```

4. On the other computer, join using the host's IPv4 address:

   ```powershell
   .\FableTogether.Launcher.exe --join 192.168.1.10 --player-id Guest
   ```

5. Each player selects the save containing the Hero they want to use.

Use `--port <port>` on both computers to choose a different UDP port.

## Build

Build `FableTogether.sln` as `Release | Win32`. Deployable files are written to `bin\Release`.

The project also includes an embedded AngelScript runtime and native scripting APIs for future gameplay mods.
