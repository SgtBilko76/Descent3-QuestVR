/*
 * Descent 3
 * Copyright (C) 2026 Descent Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// VR mode (OpenXR). Only built with D3_OPENXR (the Meta Quest port); every
// other build gets the inline no-op versions below.
//
// Model: a seated "virtual cockpit". The ship flies on the controllers; the
// head looks around freely inside it. The world is rendered once per eye into
// the headset's swapchains. Everything else the engine draws (HUD, menus,
// TelCom, movies) still goes into its normal screen framebuffer, which is shown
// as a flat screen floating in front of the player. While flying, that screen
// is transparent so the HUD overlays the world.

#pragma once

// The pose of one eye for a stereo world pass, in the ship's frame
// (Descent axes: +x right, +y up, +z forward).
struct vr_eye_view {
  int size;              // render target is size x size pixels
  float zoom;            // value for g3_StartFrame / GameRenderWorld
  float offset[3];       // eye position relative to the cockpit origin, in world units
  float right[3];        // eye orientation basis vectors
  float up[3];
  float forward[3];
};

// Analog and held inputs from the two Touch controllers.
struct vr_controller_state {
  float left_stick[2];   // x right, y up; -1..1
  float right_stick[2];
  float left_trigger;    // 0..1
  float right_trigger;
  float left_grip;
  float right_grip;
  bool a, b, x, y;
};

#if defined(D3_OPENXR)

// True while an OpenXR session is running and frames go to the headset.
bool vr_IsActive();

// Stereo world rendering. Between Begin and End, the renderer targets that
// eye's swapchain image and its screen size is the eye's (size x size), so the
// normal world renderer can run unchanged. Returns false if the eye can't be
// rendered this frame (the caller should then skip stereo for this frame).
bool vr_BeginEyePass(int eye, vr_eye_view *view);
void vr_EndEyePass();

// Called after the eye passes: clears the flat screen layer to transparent so
// that what is drawn on it next (the HUD) floats over the world.
void vr_BeginOverlay();

// While flying, what is drawn between these two calls (the crosshair) goes to
// its own flat layer at the original screen distance, while the rest of the HUD
// floats nearer (-vrhuddistance). No-ops outside the flying overlay.
void vr_BeginReticleLayer();
void vr_EndReticleLayer();

// Tells the VR input whether a game menu/dialog is open over the world (e.g.
// the pause or exit dialog). The controllers then act as pointer and menu
// keys even though the world is still being rendered. Call once per frame.
void vr_SetMenuActive(bool active);

// Touch controller state for flight input (Controls.cpp maps it to ship
// controls). Returns false when there is no VR input. Menu navigation (pointer,
// clicks, Escape/Enter) and discrete flight actions (flare, weapon cycling,
// headlight, automap, pause) are turned into mouse/key events by the VR module
// itself, so they work everywhere the engine reads the keyboard and mouse.
bool vr_GetControllerState(vr_controller_state *state);

#else

inline bool vr_IsActive() { return false; }
inline bool vr_BeginEyePass(int, vr_eye_view *) { return false; }
inline void vr_EndEyePass() {}
inline void vr_BeginOverlay() {}
inline void vr_SetMenuActive(bool) {}
inline void vr_BeginReticleLayer() {}
inline void vr_EndReticleLayer() {}
inline bool vr_GetControllerState(vr_controller_state *) { return false; }

#endif
