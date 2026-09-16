# Descent 3 for Meta Quest — version @VERSION@

A VR port of the [Descent 3 open source engine](https://github.com/DescentDevelopers/Descent3)
for Meta Quest headsets, running standalone as a sideloaded app. You sit in a
virtual cockpit: the ship flies on the Touch controllers while you look around
freely. The world and the cockpit are rendered in stereo; menus, cutscenes and
the flat HUD float in front of you.

This is a beta. It has been tested on a **Meta Quest 3** only. See
`CHANGELOG.md` for what changed between versions.

## What you need

- A Meta Quest with developer mode enabled (Quest 3 tested; Quest 2, Quest Pro
  and Quest 3S should work but are untested).
- A PC with `adb` (Android platform tools), or SideQuest.
- **Your own copy of Descent 3.** This package contains only the engine; the
  game data must come from a copy you own (original CDs, GOG or Steam).
  Descent 3 patched to v1.4 is recommended.

## Installing

(In the source tree, `@VERSION@` stands for the release version.)

```bash
adb install Descent3-Quest-@VERSION@.apk
```

The app appears in the Quest library under *Unknown Sources*. To update an
earlier release, install the new APK with `adb install -r`; your game data and
pilots are kept.

> **Uninstalling the app deletes the game data folder below** (Android removes
> an app's data directory with the app). Keep a copy of your game files.

## Game data

The game reads its data from

    /sdcard/Android/data/com.descentdevelopers.descent3/files/

It needs at least `d3.hog`. Copy all `.hog` files, plus the `missions` and
`movies` folders (and optionally the `.pld` files) into that folder.

### From the original CDs

`tools/stage_gamedata.sh` extracts the data from CD images (`.iso`/`.bin`,
including the InstallShield and Outrage installer archives on them) and
pushes it to the headset. List the base game discs first, then expansions:

```bash
tools/stage_gamedata.sh Descent3_CD1.iso Descent3_CD2.iso "Descent 3 Mercenary.iso" --push
```

### From a GOG or Steam installation

Copy the files with `adb`, then make the folders readable for the app (folders
created by `adb` are otherwise not accessible to it):

```bash
D3DIR=/path/to/Descent3
DATA=/sdcard/Android/data/com.descentdevelopers.descent3/files
adb shell mkdir -p $DATA/missions $DATA/movies
adb push "$D3DIR"/*.hog $DATA/
adb push "$D3DIR"/missions/. $DATA/missions/
adb push "$D3DIR"/movies/. $DATA/movies/
adb shell "cd $DATA && find . -mindepth 1 -type d -exec chmod 2777 {} +"
```

## Controls

| | Flying | Menus |
|---|---|---|
| Left stick | thrust forward/back, slide left/right | arrow keys |
| Right stick | turn, pitch (stick up = nose down) | arrow keys |
| Right trigger | fire primary | click (laser pointer) |
| Left trigger | fire secondary | click (laser pointer) |
| Grips | bank left / right | — |
| A | afterburner | click |
| B | flare (hold: next primary) | back |
| X / Y | slide down / up | X = Enter |
| Right stick click | drop countermeasure (hold: next countermeasure) | — |
| Left stick click | next secondary (hold: headlight) | — |
| Menu button | pause menu (hold: automap) | back |

After your ship is destroyed, pull a trigger or press A to continue.

In menus and dialogs:

| | |
|---|---|
| Left stick | previous / next item |
| Right stick | move within lists and menus |
| Trigger, A or X | select |
| B or menu button | back |
 Text fields, such as the pilot name, open the Quest's
system keyboard.
Recenter the view with the Meta button (hold).

## Known issues

- Rear view and inventory have no controller buttons yet.
- The HUD (except the cockpit) is a flat panel, and sound is not head-tracked.
- The display runs at 90 Hz where available; a `debug.oculus.refreshRate`
  device property overrides this.
- Some disc copies lack the music (`music.cab` on CD 2 is only a placeholder);
  pre-v1.4 data lacks a few images and sounds.
- Custom missions that ship Windows-only level scripts run without their
  scripts.
- Multiplayer is untested.
- Uninstalling deletes the game data folder (see above).

## License and source

The engine is licensed under the GNU General Public License v3 (`LICENSE`).
The complete source for this build is included in `source/` and is available
from the project repository. Third-party components are listed in
`THIRD_PARTY.md`; this port additionally uses SDL 3 (zlib license), the
Khronos OpenXR loader (Apache 2.0), glm (MIT), plog (MIT) and cpp-httplib (MIT).

Descent 3 is a trademark of its respective owners. This port is not affiliated
with or endorsed by Interplay, Parallax Software, Outrage Entertainment or
Meta.
