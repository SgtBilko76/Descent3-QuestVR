# Descent 3 VR -- Meta Quest and PICO

[![Sponsor](https://img.shields.io/badge/Sponsor-SgtBilko76-ea4aaa?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/SgtBilko76)


This fork is a standalone VR port of the Descent 3 open source engine for
Meta Quest and PICO headsets: OpenXR, stereo world and cockpit rendering,
controller flight and menus. You sit in a virtual cockpit -- the ship flies on
the sticks while your head looks around freely. You need your own copy of the
Descent 3 game data.

- Download: `Descent3-VR-<version>.apk` on the [Releases](../../releases) page.
- Installing, game data, controls: [quest/README.md](quest/README.md)
- Changes: [quest/CHANGELOG.md](quest/CHANGELOG.md)
- Building: `quest/build.sh` (Android SDK/NDK, see `quest/env.sh`)

Tested on a Meta Quest 3. The same APK carries PICO support, which has never
been run on a PICO headset -- reports welcome.

The port is developed on the `quest-vr-port` branch. The original engine
README follows.

---

![d3 (1)](https://github.com/DescentDevelopers/Descent3/assets/47716344/82ba0911-ee32-4565-84ee-b432c215ab95)

This is the Descent 3 open source engine, licensed under [GPL-3.0](https://github.com/DescentDevelopers/Descent3?tab=GPL-3.0-1-ov-file). It includes the '1.5' patch written by Kevin Bentley and Jeff Slutter several years ago and brought to a stable condition by the Descent community.

In order to use this, you must provide your own game files. See the [USAGE.md](USAGE.md) file for details about installation.

To build the game, follow build instructions in the [BUILD.md](BUILD.md) file.

Build or runtime issues should be reported on our [GitHub tracker](https://github.com/DescentDevelopers/Descent3/issues).

## Contributing
Anyone can contribute! We have an active Discord presence at [Descent Developer Network](https://discord.gg/GNy5CUQ). Patches should be submitted on GitHub.