# Descent 3 for Meta Quest — changelog

## 0.3-beta

- PICO headsets: the same APK now also declares itself as a PICO VR app and
  offers the PICO 4 / Neo 3 controller bindings (same button layout).
  **Untested**: no PICO hardware was available, and it is unknown whether
  PICO OS finds the runtime through the bundled Khronos OpenXR loader.
- Head tracking is no longer declared as a required feature, so the APK also
  installs on non-Meta headsets.
- The downloads are now named `Descent3-VR-<version>` instead of
  `Descent3-Quest-<version>`, since one build serves both headsets.

## 0.2-beta

- Menus and dialogs are controlled with the controllers only: left stick
  moves between items, right stick moves within lists and menus, trigger/A/X
  selects, B or the menu button goes back. The mouse pointer is gone.
- In-game dialogs (e.g. the exit confirmation) can be used; before, the
  controllers stayed in flight mode.
- Pull a trigger or press A to continue after your ship is destroyed.
- Countermeasures: right stick click drops one, hold it to select the next.
  Other buttons (tap / hold): B flare / next primary, left stick click next
  secondary / headlight, menu button pause menu / automap.
- The cockpit and the flat HUD are nearer (about 0.9 m); the crosshair stays
  at its previous distance.
- Text fields such as the pilot name open the Quest system keyboard.

## 0.1-beta

- First release: stereo rendering with head tracking, stereo cockpit, flat
  HUD and menus, Touch controller flight controls, 72/90 Hz.
