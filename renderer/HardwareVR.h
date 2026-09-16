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

// Interface between the GL backend (HardwareOpenGL.cpp) and the OpenXR VR
// mode (HardwareVR.cpp). The game-facing API is lib/d3vr.h.

#pragma once

#if defined(D3_OPENXR)

// Starts OpenXR on the current GL context. Safe to call repeatedly; returns
// whether VR is available.
bool vrgl_Init();
void vrgl_Shutdown();

// Presents the frame to the headset, replacing the window blit and buffer swap.
// screen_fbo is the engine's screen framebuffer (w x h), shown as the flat
// screen layer. Returns false if VR is not in use and the caller should present
// to the window as usual.
bool vrgl_Present(unsigned int screen_fbo, int w, int h);

// While true, blending into the screen framebuffer must keep its alpha channel
// meaningful (premultiplied), because that layer is composited over the world.
bool vrgl_OverlayIsTransparent();

// Provided by HardwareOpenGL.cpp: rebinds the engine's screen framebuffer and
// restores its viewport, and forces the next rend_SetAlphaType to reapply.
void opengl_BindScreenFramebuffer();
void opengl_InvalidateBlendState();

#endif
