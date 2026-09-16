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

// OpenXR VR mode for the Meta Quest port. See lib/d3vr.h for the model.
//
// Frame flow. rend_Flip() ends every engine frame, so it is the frame boundary:
//   vrgl_Present(): submit layers, xrEndFrame, then xrWaitFrame/xrBeginFrame
//                   for the next frame and read the controllers.
//   eye passes:     locate the views (once per frame, as late as possible) and
//                   render the world into the eye swapchains.
//   everything else the engine draws lands in its screen framebuffer, which
//   vrgl_Present copies into the flat "screen" quad layer.
//
// Coordinates. OpenXR's LOCAL space is right-handed (+x right, +y up,
// -z forward); Descent's is left-handed (+z forward). Converting a direction
// is (x, y, -z). LOCAL space is the cockpit: the ship's frame, with the origin
// at the player's head when the view was last recentered.

#if defined(D3_OPENXR)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl32.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <SDL3/SDL.h>

#include "args.h"
#include "d3vr.h"
#include "ddio.h"
#include "3d.h"
#include "HardwareInternal.h"
#include "HardwareVR.h"
#include "log.h"
#include "renderer.h"

extern rendering_state gpu_state;

#ifndef GL_FRAMEBUFFER_SRGB_EXT
#define GL_FRAMEBUFFER_SRGB_EXT 0x8DB9
#endif

namespace {

// Tunables. The flat screen floats this far in front of the cockpit origin.
constexpr float kScreenDistance = 1.5f;   // meters
constexpr float kScreenWidth = 1.8f;      // meters (about 62 degrees wide)
constexpr float kLongPressSeconds = 0.5f;
constexpr float kStickPress = 0.6f;       // stick deflection that counts as a key press...
constexpr float kStickRelease = 0.4f;     // ...and back below this releases it

struct Swapchain {
  XrSwapchain handle = XR_NULL_HANDLE;
  int32_t width = 0;
  int32_t height = 0;
  std::vector<XrSwapchainImageOpenGLESKHR> images;
};

enum Hand { kLeft = 0, kRight = 1 };

struct Input {
  XrActionSet set = XR_NULL_HANDLE;
  XrAction stick = XR_NULL_HANDLE;       // vector2f, both hands
  XrAction trigger = XR_NULL_HANDLE;     // float, both hands
  XrAction grip = XR_NULL_HANDLE;        // float, both hands
  XrAction stick_click = XR_NULL_HANDLE; // bool, both hands
  XrAction aim = XR_NULL_HANDLE;         // pose, both hands
  XrAction a = XR_NULL_HANDLE, b = XR_NULL_HANDLE, x = XR_NULL_HANDLE, y = XR_NULL_HANDLE;
  XrAction menu = XR_NULL_HANDLE;
  XrPath hand[2] = {XR_NULL_PATH, XR_NULL_PATH};
  XrSpace aim_space[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};

  vr_controller_state state{};
  bool click_down[2] = {false, false};    // thumbstick clicks, last frame

  // Discrete-action bookkeeping.
  int pointer_hand = kRight;
  bool mouse_down = false;
  std::vector<SDL_Keycode> held_keys;           // keys currently pressed on the engine's behalf
  std::vector<SDL_Keycode> release_next_frame;  // taps: released one frame after the press
  float click_start[2] = {-1, -1};              // stick click press time, for short/long press
  bool click_long_fired[2] = {false, false};
  int stick_dir[2] = {0, 0};                    // menu-mode arrow key currently held per stick
};

struct VRState {
  bool tried_init = false;
  bool initialized = false;

  jobject activity = nullptr;
  XrInstance instance = XR_NULL_HANDLE;
  XrSystemId system = XR_NULL_SYSTEM_ID;
  XrSession session = XR_NULL_HANDLE;
  XrSpace local_space = XR_NULL_HANDLE;
  XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
  bool session_running = false;
  // Extension entry points; the loader only exports core functions.
  PFN_xrRequestDisplayRefreshRateFB request_refresh_rate = nullptr;
  PFN_xrPerfSettingsSetPerformanceLevelEXT set_perf_level = nullptr;

  // Frame.
  bool frame_begun = false;
  XrFrameState frame_state{XR_TYPE_FRAME_STATE};
  bool views_located = false;
  std::array<XrView, 2> views{};
  float eye_tan = 1.0f;         // tan of the half-angle of the square eye frustum
  bool eye_rendered[2] = {false, false};
  bool world_this_frame = false;
  bool world_last_frame = false;
  bool overlay_transparent = false;

  // Rendering.
  GLenum color_format = GL_RGBA8;
  bool srgb_write_control = false;
  int eye_size = 0;
  Swapchain eye[2];
  Swapchain screen;
  GLuint eye_fbo = 0;
  GLuint eye_depth = 0;
  GLuint screen_fbo = 0;
  int active_eye = -1;
  int saved_screen_w = 0;
  int saved_screen_h = 0;
  float world_scale = 1.0f;     // world units per meter

  Input input;
  float time = 0;               // seconds, for press durations

  // Frame statistics, logged periodically.
  int stat_frames = 0;
  int stat_stereo = 0;
  float stat_start = -1;
};

VRState vr;

// ---------------------------------------------------------------------------
// Helpers

bool XrOk(XrResult r, const char *what) {
  if (XR_SUCCEEDED(r)) {
    return true;
  }
  char name[XR_MAX_RESULT_STRING_SIZE] = "?";
  if (vr.instance != XR_NULL_HANDLE) {
    xrResultToString(vr.instance, r, name);
  }
  LOG_ERROR.printf("VR: %s failed: %s (%d)", what, name, static_cast<int>(r));
  return false;
}

template <typename Fn>
Fn GetProc(XrInstance instance, const char *name) {
  PFN_xrVoidFunction fn = nullptr;
  if (XR_FAILED(xrGetInstanceProcAddr(instance, name, &fn))) {
    return nullptr;
  }
  return reinterpret_cast<Fn>(fn);
}

XrPath Path(const char *s) {
  XrPath p = XR_NULL_PATH;
  xrStringToPath(vr.instance, s, &p);
  return p;
}

bool HasGLExtension(const char *name) {
  GLint n = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &n);
  for (GLint i = 0; i < n; i++) {
    if (strcmp(reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i)), name) == 0) {
      return true;
    }
  }
  return false;
}

// Rotates v by unit quaternion q.
XrVector3f Rotate(const XrQuaternionf &q, const XrVector3f &v) {
  const XrVector3f u{q.x, q.y, q.z};
  const float s = q.w;
  const float dot_uv = u.x * v.x + u.y * v.y + u.z * v.z;
  const float dot_uu = u.x * u.x + u.y * u.y + u.z * u.z;
  const XrVector3f cross{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
  return {2 * dot_uv * u.x + (s * s - dot_uu) * v.x + 2 * s * cross.x,
          2 * dot_uv * u.y + (s * s - dot_uu) * v.y + 2 * s * cross.y,
          2 * dot_uv * u.z + (s * s - dot_uu) * v.z + 2 * s * cross.z};
}

// OpenXR (right-handed, -z forward) -> Descent (left-handed, +z forward).
void ToDescent(const XrVector3f &v, float scale, float out[3]) {
  out[0] = v.x * scale;
  out[1] = v.y * scale;
  out[2] = -v.z * scale;
}

float ArgFloat(const char *name, float def) {
  const int i = FindArg(name);
  return i ? static_cast<float>(atof(GameArgs[i + 1])) : def;
}

// ---------------------------------------------------------------------------
// Swapchains

bool CreateSwapchain(Swapchain &sc, int w, int h) {
  XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
  info.format = vr.color_format;
  info.sampleCount = 1;
  info.width = w;
  info.height = h;
  info.faceCount = 1;
  info.arraySize = 1;
  info.mipCount = 1;
  if (!XrOk(xrCreateSwapchain(vr.session, &info, &sc.handle), "xrCreateSwapchain")) {
    return false;
  }
  uint32_t count = 0;
  xrEnumerateSwapchainImages(sc.handle, 0, &count, nullptr);
  sc.images.assign(count, XrSwapchainImageOpenGLESKHR{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
  if (!XrOk(xrEnumerateSwapchainImages(sc.handle, count, &count,
                                       reinterpret_cast<XrSwapchainImageBaseHeader *>(sc.images.data())),
            "xrEnumerateSwapchainImages")) {
    return false;
  }
  sc.width = w;
  sc.height = h;
  return true;
}

void DestroySwapchain(Swapchain &sc) {
  if (sc.handle != XR_NULL_HANDLE) {
    xrDestroySwapchain(sc.handle);
  }
  sc = Swapchain{};
}

// Acquires the next image and attaches it to fbo. Returns the GL texture or 0.
GLuint AcquireInto(Swapchain &sc, GLuint fbo) {
  uint32_t index = 0;
  XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (!XrOk(xrAcquireSwapchainImage(sc.handle, &acquire, &index), "xrAcquireSwapchainImage")) {
    return 0;
  }
  XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wait.timeout = XR_INFINITE_DURATION;
  if (!XrOk(xrWaitSwapchainImage(sc.handle, &wait), "xrWaitSwapchainImage")) {
    return 0;
  }
  const GLuint tex = sc.images[index].image;
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    static int reported = 0;
    if (reported++ < 5) {
      LOG_ERROR.printf("VR: swapchain framebuffer incomplete (0x%x, GL error 0x%x)", status, glGetError());
    }
  }
  if (vr.srgb_write_control) {
    // Store the engine's (already display-encoded) values as they are; the
    // compositor decodes the sRGB image.
    glDisable(GL_FRAMEBUFFER_SRGB_EXT);
  }
  return tex;
}

void Release(Swapchain &sc) {
  XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  XrOk(xrReleaseSwapchainImage(sc.handle, &release), "xrReleaseSwapchainImage");
}

// Clears the bound framebuffer, independent of the engine's cached GL state.
void ClearBound(int w, int h, float r, float g, float b, float a) {
  GLboolean depth_mask = GL_TRUE;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
  const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDepthMask(GL_TRUE);
  glViewport(0, 0, w, h);
  glClearColor(r, g, b, a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glDepthMask(depth_mask);
  if (scissor) {
    glEnable(GL_SCISSOR_TEST);
  }
}

// ---------------------------------------------------------------------------
// Input

XrAction CreateAction(const char *name, const char *localized, XrActionType type, bool both_hands) {
  XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
  info.actionType = type;
  strncpy(info.actionName, name, sizeof(info.actionName) - 1);
  strncpy(info.localizedActionName, localized, sizeof(info.localizedActionName) - 1);
  if (both_hands) {
    info.countSubactionPaths = 2;
    info.subactionPaths = vr.input.hand;
  }
  XrAction action = XR_NULL_HANDLE;
  XrOk(xrCreateAction(vr.input.set, &info, &action), name);
  return action;
}

bool InitInput() {
  Input &in = vr.input;
  XrActionSetCreateInfo set_info{XR_TYPE_ACTION_SET_CREATE_INFO};
  strcpy(set_info.actionSetName, "descent3");
  strcpy(set_info.localizedActionSetName, "Descent 3");
  if (!XrOk(xrCreateActionSet(vr.instance, &set_info, &in.set), "xrCreateActionSet")) {
    return false;
  }
  in.hand[kLeft] = Path("/user/hand/left");
  in.hand[kRight] = Path("/user/hand/right");

  in.stick = CreateAction("stick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, true);
  in.trigger = CreateAction("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, true);
  in.grip = CreateAction("grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT, true);
  in.stick_click = CreateAction("stick_click", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT, true);
  in.aim = CreateAction("aim", "Pointer", XR_ACTION_TYPE_POSE_INPUT, true);
  in.a = CreateAction("button_a", "A", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
  in.b = CreateAction("button_b", "B", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
  in.x = CreateAction("button_x", "X", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
  in.y = CreateAction("button_y", "Y", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
  in.menu = CreateAction("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, false);

  const std::vector<std::pair<XrAction, const char *>> bindings = {
      {in.stick, "/user/hand/left/input/thumbstick"},
      {in.stick, "/user/hand/right/input/thumbstick"},
      {in.trigger, "/user/hand/left/input/trigger/value"},
      {in.trigger, "/user/hand/right/input/trigger/value"},
      {in.grip, "/user/hand/left/input/squeeze/value"},
      {in.grip, "/user/hand/right/input/squeeze/value"},
      {in.stick_click, "/user/hand/left/input/thumbstick/click"},
      {in.stick_click, "/user/hand/right/input/thumbstick/click"},
      {in.aim, "/user/hand/left/input/aim/pose"},
      {in.aim, "/user/hand/right/input/aim/pose"},
      {in.a, "/user/hand/right/input/a/click"},
      {in.b, "/user/hand/right/input/b/click"},
      {in.x, "/user/hand/left/input/x/click"},
      {in.y, "/user/hand/left/input/y/click"},
      {in.menu, "/user/hand/left/input/menu/click"},
  };
  std::vector<XrActionSuggestedBinding> suggested;
  for (const auto &[action, path] : bindings) {
    suggested.push_back({action, Path(path)});
  }
  XrInteractionProfileSuggestedBinding profile{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  profile.interactionProfile = Path("/interaction_profiles/oculus/touch_controller");
  profile.suggestedBindings = suggested.data();
  profile.countSuggestedBindings = static_cast<uint32_t>(suggested.size());
  if (!XrOk(xrSuggestInteractionProfileBindings(vr.instance, &profile), "xrSuggestInteractionProfileBindings")) {
    return false;
  }

  for (int h = 0; h < 2; h++) {
    XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    space.action = in.aim;
    space.subactionPath = in.hand[h];
    space.poseInActionSpace.orientation.w = 1;
    XrOk(xrCreateActionSpace(vr.session, &space, &in.aim_space[h]), "xrCreateActionSpace");
  }

  XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  attach.countActionSets = 1;
  attach.actionSets = &in.set;
  return XrOk(xrAttachSessionActionSets(vr.session, &attach), "xrAttachSessionActionSets");
}

float GetFloat(XrAction action, int hand) {
  XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
  info.action = action;
  info.subactionPath = hand >= 0 ? vr.input.hand[hand] : XR_NULL_PATH;
  XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
  xrGetActionStateFloat(vr.session, &info, &state);
  return state.isActive ? state.currentState : 0.0f;
}

bool GetBool(XrAction action, int hand) {
  XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
  info.action = action;
  info.subactionPath = hand >= 0 ? vr.input.hand[hand] : XR_NULL_PATH;
  XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
  xrGetActionStateBoolean(vr.session, &info, &state);
  return state.isActive && state.currentState;
}

void GetStick(int hand, float out[2]) {
  XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
  info.action = vr.input.stick;
  info.subactionPath = vr.input.hand[hand];
  XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
  xrGetActionStateVector2f(vr.session, &info, &state);
  out[0] = state.isActive ? state.currentState.x : 0.0f;
  out[1] = state.isActive ? state.currentState.y : 0.0f;
}

void SendKey(SDL_Keycode key, bool down) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  e.key.key = key;
  e.key.down = down;
  e.key.timestamp = SDL_GetTicksNS();
  SDL_PushEvent(&e);
}

void HoldKey(SDL_Keycode key, bool down) {
  auto &held = vr.input.held_keys;
  const auto it = std::find(held.begin(), held.end(), key);
  if (down && it == held.end()) {
    held.push_back(key);
    SendKey(key, true);
  } else if (!down && it != held.end()) {
    held.erase(it);
    SendKey(key, false);
  }
}

// A key press the engine sees for exactly one frame.
void TapKey(SDL_Keycode key) {
  SendKey(key, true);
  vr.input.release_next_frame.push_back(key);
}

void SendMouseButton(bool down) {
  if (vr.input.mouse_down == down) {
    return;
  }
  vr.input.mouse_down = down;
  SDL_Event e{};
  e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
  e.button.button = SDL_BUTTON_LEFT;
  e.button.down = down;
  e.button.clicks = 1;
  e.button.timestamp = SDL_GetTicksNS();
  SDL_PushEvent(&e);
}

void ReleaseAll() {
  for (SDL_Keycode key : std::vector<SDL_Keycode>(vr.input.held_keys)) {
    HoldKey(key, false);
  }
  SendMouseButton(false);
  vr.input.stick_dir[0] = vr.input.stick_dir[1] = 0;
}

// Points the engine's mouse at where the controller ray hits the flat screen.
void UpdatePointer(int hand, int screen_w, int screen_h) {
  XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
  if (XR_FAILED(xrLocateSpace(vr.input.aim_space[hand], vr.local_space, vr.frame_state.predictedDisplayTime, &loc)) ||
      !(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) ||
      !(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
    return;
  }
  const XrVector3f dir = Rotate(loc.pose.orientation, {0, 0, -1});
  if (dir.z >= -1e-4f) {
    return; // pointing away from the screen
  }
  const float t = (-kScreenDistance - loc.pose.position.z) / dir.z;
  const float hx = loc.pose.position.x + dir.x * t;
  const float hy = loc.pose.position.y + dir.y * t;
  const float screen_h_m = kScreenWidth * static_cast<float>(screen_h) / static_cast<float>(screen_w);
  const float u = hx / kScreenWidth + 0.5f;
  const float v = 0.5f - hy / screen_h_m;
  if (u < 0 || u > 1 || v < 0 || v > 1) {
    return;
  }
  ddio_MouseSetPosition(static_cast<int>(u * screen_w), static_cast<int>(v * screen_h));
}

// Maps a stick to one held arrow key (menus).
void StickToArrows(int hand, const float stick[2]) {
  int &cur = vr.input.stick_dir[hand];
  const float mag = std::max(std::fabs(stick[0]), std::fabs(stick[1]));
  int want = cur;
  if (cur == 0 && mag > kStickPress) {
    want = std::fabs(stick[0]) > std::fabs(stick[1]) ? (stick[0] > 0 ? 2 : 4) : (stick[1] > 0 ? 1 : 3);
  } else if (cur != 0 && mag < kStickRelease) {
    want = 0;
  }
  if (want == cur) {
    return;
  }
  static constexpr SDL_Keycode keys[] = {0, SDLK_UP, SDLK_RIGHT, SDLK_DOWN, SDLK_LEFT};
  if (cur) {
    HoldKey(keys[cur], false);
  }
  if (want) {
    HoldKey(keys[want], true);
  }
  cur = want;
}

// Short press -> short_key, press held past kLongPressSeconds -> long_key.
void ClickShortLong(int hand, bool pressed, SDL_Keycode short_key, SDL_Keycode long_key) {
  Input &in = vr.input;
  if (pressed) {
    if (in.click_start[hand] < 0) {
      in.click_start[hand] = vr.time;
      in.click_long_fired[hand] = false;
    } else if (!in.click_long_fired[hand] && vr.time - in.click_start[hand] >= kLongPressSeconds) {
      in.click_long_fired[hand] = true;
      TapKey(long_key);
    }
  } else if (in.click_start[hand] >= 0) {
    if (!in.click_long_fired[hand]) {
      TapKey(short_key);
    }
    in.click_start[hand] = -1;
  }
}

void PollInput() {
  Input &in = vr.input;
  for (SDL_Keycode key : in.release_next_frame) {
    SendKey(key, false);
  }
  in.release_next_frame.clear();

  if (vr.session_state != XR_SESSION_STATE_FOCUSED) {
    // No input while the system UI has focus.
    ReleaseAll();
    in.state = vr_controller_state{};
    return;
  }

  XrActiveActionSet active{in.set, XR_NULL_PATH};
  XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
  sync.countActiveActionSets = 1;
  sync.activeActionSets = &active;
  if (XR_FAILED(xrSyncActions(vr.session, &sync))) {
    return;
  }

  vr_controller_state &s = in.state;
  GetStick(kLeft, s.left_stick);
  GetStick(kRight, s.right_stick);
  s.left_trigger = GetFloat(in.trigger, kLeft);
  s.right_trigger = GetFloat(in.trigger, kRight);
  s.left_grip = GetFloat(in.grip, kLeft);
  s.right_grip = GetFloat(in.grip, kRight);
  s.a = GetBool(in.a, -1);
  s.b = GetBool(in.b, -1);
  s.x = GetBool(in.x, -1);
  s.y = GetBool(in.y, -1);
  const bool click[2] = {GetBool(in.stick_click, kLeft), GetBool(in.stick_click, kRight)};
  const bool menu = GetBool(in.menu, -1);

  // The mode follows what is on screen: flying if the world was rendered.
  static bool flying = false;
  if (flying != vr.world_last_frame) {
    ReleaseAll();
    in.click_start[0] = in.click_start[1] = -1;
    flying = vr.world_last_frame;
  }

  if (flying) {
    // Analog flight, fire, afterburner and vertical thrust are read by
    // Controls.cpp through vr_GetControllerState(). Discrete actions use the
    // engine's default key bindings.
    HoldKey(SDLK_ESCAPE, menu);  // pause menu
    HoldKey(SDLK_F, s.b);        // flare
    ClickShortLong(kRight, click[kRight], SDLK_COMMA, SDLK_H);   // cycle primary / headlight
    ClickShortLong(kLeft, click[kLeft], SDLK_PERIOD, SDLK_TAB);  // cycle secondary / automap
  } else {
    // Menus: laser pointer on the flat screen, trigger or A clicks,
    // B backs out, X confirms, sticks send arrow keys.
    if (s.left_trigger > 0.5f && s.right_trigger < 0.5f) {
      in.pointer_hand = kLeft;
    } else if (s.right_trigger > 0.5f && s.left_trigger < 0.5f) {
      in.pointer_hand = kRight;
    }
    const bool press = (in.pointer_hand == kLeft ? s.left_trigger : s.right_trigger) > 0.5f || s.a;
    SendMouseButton(press);
    HoldKey(SDLK_ESCAPE, menu || s.b);
    HoldKey(SDLK_RETURN, s.x);
    StickToArrows(kLeft, s.left_stick);
    StickToArrows(kRight, s.right_stick);
  }
  in.click_down[0] = click[0];
  in.click_down[1] = click[1];
}

// ---------------------------------------------------------------------------
// Session and frames

void HandleEvents() {
  XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
  while (xrPollEvent(vr.instance, &event) == XR_SUCCESS) {
    switch (event.type) {
    case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
      const auto &changed = *reinterpret_cast<XrEventDataSessionStateChanged *>(&event);
      vr.session_state = changed.state;
      LOG_INFO.printf("VR: session state %d", static_cast<int>(changed.state));
      switch (changed.state) {
      case XR_SESSION_STATE_READY: {
        XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
        begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        vr.session_running = XrOk(xrBeginSession(vr.session, &begin), "xrBeginSession");
        if (vr.session_running && vr.request_refresh_rate) {
          const float hz = ArgFloat("-vrhz", 72.0f);
          XrOk(vr.request_refresh_rate(vr.session, hz), "xrRequestDisplayRefreshRateFB");
        }
        if (vr.session_running && vr.set_perf_level) {
          vr.set_perf_level(vr.session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
          vr.set_perf_level(vr.session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
        }
        break;
      }
      case XR_SESSION_STATE_STOPPING:
        ReleaseAll();
        vr.session_running = false;
        xrEndSession(vr.session);
        break;
      case XR_SESSION_STATE_EXITING:
      case XR_SESSION_STATE_LOSS_PENDING: {
        vr.session_running = false;
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
        break;
      }
      default:
        break;
      }
      break;
    }
    case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
      vr.session_running = false;
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
      break;
    }
    default:
      break;
    }
    event = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
  }
}

bool BeginFrame() {
  if (vr.frame_begun) {
    return true;
  }
  HandleEvents();
  if (!vr.session_running) {
    return false;
  }
  vr.frame_state = XrFrameState{XR_TYPE_FRAME_STATE};
  XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
  if (!XrOk(xrWaitFrame(vr.session, &wait, &vr.frame_state), "xrWaitFrame")) {
    return false;
  }
  XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
  if (!XrOk(xrBeginFrame(vr.session, &begin), "xrBeginFrame")) {
    return false;
  }
  vr.frame_begun = true;
  vr.views_located = false;
  vr.eye_rendered[0] = vr.eye_rendered[1] = false;
  vr.world_this_frame = false;
  vr.time = static_cast<float>(SDL_GetTicks()) / 1000.0f;
  PollInput();
  return true;
}

bool LocateViews() {
  if (vr.views_located) {
    return true;
  }
  XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};
  info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  info.displayTime = vr.frame_state.predictedDisplayTime;
  info.space = vr.local_space;
  XrViewState state{XR_TYPE_VIEW_STATE};
  uint32_t count = 0;
  vr.views.fill(XrView{XR_TYPE_VIEW});
  if (!XrOk(xrLocateViews(vr.session, &info, &state, 2, &count, vr.views.data()), "xrLocateViews") || count != 2 ||
      !(state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
    return false;
  }
  // One square, symmetric frustum that covers both eyes' fields of view. The
  // engine's projection is symmetric (and only consistent for square or wide
  // viewports); OpenXR is told this exact FOV, so the compositor reprojects it.
  float t = 0;
  for (const XrView &v : vr.views) {
    for (float a : {v.fov.angleLeft, v.fov.angleRight, v.fov.angleUp, v.fov.angleDown}) {
      t = std::max(t, std::tan(std::fabs(a)));
    }
  }
  vr.eye_tan = t;
  vr.views_located = true;
  return true;
}

bool EnsureScreenSwapchain(int w, int h) {
  if (vr.screen.handle != XR_NULL_HANDLE && vr.screen.width == w && vr.screen.height == h) {
    return true;
  }
  DestroySwapchain(vr.screen);
  if (!CreateSwapchain(vr.screen, w, h)) {
    return false;
  }
  LOG_INFO.printf("VR: screen layer %d x %d", w, h);
  return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Renderer-internal API

bool vrgl_Init() {
  if (vr.tried_init) {
    return vr.initialized;
  }
  vr.tried_init = true;
  if (FindArg("-novr")) {
    LOG_INFO << "VR: disabled by -novr";
    return false;
  }

  auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
  JavaVM *jvm = nullptr;
  if (!env || env->GetJavaVM(&jvm) != JNI_OK) {
    LOG_ERROR << "VR: no JNI environment";
    return false;
  }
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  vr.activity = env->NewGlobalRef(activity);
  env->DeleteLocalRef(activity);

  // Loader.
  {
    XrLoaderInitInfoAndroidKHR init{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    init.applicationVM = jvm;
    init.applicationContext = vr.activity;
    const auto init_loader = GetProc<PFN_xrInitializeLoaderKHR>(XR_NULL_HANDLE, "xrInitializeLoaderKHR");
    if (!init_loader ||
        !XrOk(init_loader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR *>(&init)), "xrInitializeLoaderKHR")) {
      return false;
    }
  }

  // Instance.
  uint32_t ext_count = 0;
  xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr);
  std::vector<XrExtensionProperties> props(ext_count, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
  xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, props.data());
  auto available = [&](const char *name) {
    return std::any_of(props.begin(), props.end(),
                       [&](const XrExtensionProperties &p) { return strcmp(p.extensionName, name) == 0; });
  };
  std::vector<const char *> extensions = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                                          XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
  for (const char *required : extensions) {
    if (!available(required)) {
      LOG_ERROR.printf("VR: runtime lacks %s", required);
      return false;
    }
  }
  const bool has_refresh_rate = available(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
  if (has_refresh_rate) {
    extensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
  }
  const bool has_perf_settings = available(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
  if (has_perf_settings) {
    extensions.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
  }

  XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
  android.applicationVM = jvm;
  android.applicationActivity = vr.activity;
  XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
  create.next = &android;
  strcpy(create.applicationInfo.applicationName, "Descent 3");
  create.applicationInfo.applicationVersion = 1;
  strcpy(create.applicationInfo.engineName, "Descent 3");
  create.applicationInfo.engineVersion = 1;
  create.applicationInfo.apiVersion = XR_API_VERSION_1_0;
  create.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  create.enabledExtensionNames = extensions.data();
  if (!XrOk(xrCreateInstance(&create, &vr.instance), "xrCreateInstance")) {
    return false;
  }
  if (has_refresh_rate) {
    vr.request_refresh_rate =
        GetProc<PFN_xrRequestDisplayRefreshRateFB>(vr.instance, "xrRequestDisplayRefreshRateFB");
  }
  if (has_perf_settings) {
    vr.set_perf_level =
        GetProc<PFN_xrPerfSettingsSetPerformanceLevelEXT>(vr.instance, "xrPerfSettingsSetPerformanceLevelEXT");
  }
  XrInstanceProperties iprops{XR_TYPE_INSTANCE_PROPERTIES};
  xrGetInstanceProperties(vr.instance, &iprops);
  LOG_INFO.printf("VR: runtime %s %u.%u.%u", iprops.runtimeName, XR_VERSION_MAJOR(iprops.runtimeVersion),
                  XR_VERSION_MINOR(iprops.runtimeVersion), XR_VERSION_PATCH(iprops.runtimeVersion));

  XrSystemGetInfo sys{XR_TYPE_SYSTEM_GET_INFO};
  sys.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  if (!XrOk(xrGetSystem(vr.instance, &sys, &vr.system), "xrGetSystem")) {
    return false;
  }

  // Session on the engine's EGL context.
  XrGraphicsRequirementsOpenGLESKHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
  const auto get_reqs =
      GetProc<PFN_xrGetOpenGLESGraphicsRequirementsKHR>(vr.instance, "xrGetOpenGLESGraphicsRequirementsKHR");
  if (!get_reqs || !XrOk(get_reqs(vr.instance, vr.system, &reqs), "graphics requirements")) {
    return false;
  }
  XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
  binding.display = eglGetCurrentDisplay();
  binding.config = static_cast<EGLConfig>(SDL_EGL_GetCurrentConfig());
  binding.context = eglGetCurrentContext();
  if (binding.display == EGL_NO_DISPLAY || !binding.config || binding.context == EGL_NO_CONTEXT) {
    LOG_ERROR << "VR: no current EGL context";
    return false;
  }
  XrSessionCreateInfo session{XR_TYPE_SESSION_CREATE_INFO};
  session.next = &binding;
  session.systemId = vr.system;
  if (!XrOk(xrCreateSession(vr.instance, &session, &vr.session), "xrCreateSession")) {
    return false;
  }

  XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  space.poseInReferenceSpace.orientation.w = 1;
  if (!XrOk(xrCreateReferenceSpace(vr.session, &space, &vr.local_space), "xrCreateReferenceSpace")) {
    return false;
  }

  // Swapchain format: sRGB if the stored values can be written unconverted.
  vr.srgb_write_control = HasGLExtension("GL_EXT_sRGB_write_control");
  uint32_t fmt_count = 0;
  xrEnumerateSwapchainFormats(vr.session, 0, &fmt_count, nullptr);
  std::vector<int64_t> formats(fmt_count);
  xrEnumerateSwapchainFormats(vr.session, fmt_count, &fmt_count, formats.data());
  auto has_format = [&](int64_t f) { return std::find(formats.begin(), formats.end(), f) != formats.end(); };
  if (vr.srgb_write_control && has_format(GL_SRGB8_ALPHA8)) {
    vr.color_format = GL_SRGB8_ALPHA8;
  } else if (has_format(GL_RGBA8)) {
    vr.color_format = GL_RGBA8;
    vr.srgb_write_control = false;
  } else {
    LOG_ERROR << "VR: no usable swapchain format";
    return false;
  }

  // Eye swapchains.
  uint32_t view_count = 0;
  xrEnumerateViewConfigurationViews(vr.instance, vr.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count,
                                    nullptr);
  std::vector<XrViewConfigurationView> config(view_count, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
  xrEnumerateViewConfigurationViews(vr.instance, vr.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view_count,
                                    &view_count, config.data());
  if (view_count != 2) {
    LOG_ERROR.printf("VR: expected 2 views, got %u", view_count);
    return false;
  }
  const float scale = std::clamp(ArgFloat("-vrscale", 0.7f), 0.25f, 1.5f);
  const int recommended = static_cast<int>(std::max(config[0].recommendedImageRectWidth,
                                                    config[0].recommendedImageRectHeight));
  vr.eye_size = std::min(static_cast<int>(recommended * scale) & ~15,
                         static_cast<int>(config[0].maxImageRectWidth) & ~15);
  vr.world_scale = ArgFloat("-vrworldscale", 1.0f);
  for (auto &eye : vr.eye) {
    if (!CreateSwapchain(eye, vr.eye_size, vr.eye_size)) {
      return false;
    }
  }

  glGenFramebuffers(1, &vr.eye_fbo);
  glGenRenderbuffers(1, &vr.eye_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, vr.eye_depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, vr.eye_size, vr.eye_size);
  glBindFramebuffer(GL_FRAMEBUFFER, vr.eye_fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, vr.eye_depth);
  glGenFramebuffers(1, &vr.screen_fbo);
  opengl_BindScreenFramebuffer();

  if (!InitInput()) {
    return false;
  }

  vr.initialized = true;
  LOG_INFO.printf("VR: ready. Eye buffers %d x %d (recommended %d, scale %.2f), %s swapchains", vr.eye_size,
                  vr.eye_size, recommended, scale, vr.color_format == GL_SRGB8_ALPHA8 ? "sRGB" : "RGBA8");
  return true;
}

void vrgl_Shutdown() {
  if (!vr.initialized) {
    return;
  }
  ReleaseAll();
  DestroySwapchain(vr.eye[0]);
  DestroySwapchain(vr.eye[1]);
  DestroySwapchain(vr.screen);
  if (vr.session != XR_NULL_HANDLE) {
    xrDestroySession(vr.session);
  }
  if (vr.instance != XR_NULL_HANDLE) {
    xrDestroyInstance(vr.instance);
  }
  vr = VRState{};
  vr.tried_init = true;
}

bool vrgl_OverlayIsTransparent() { return vr.overlay_transparent; }

bool vrgl_Present(unsigned int screen_fbo, int w, int h) {
  if (!vr.initialized) {
    return false;
  }
  if (!BeginFrame()) {
    // Session not running (e.g. headset off): nothing to show. Don't spin.
    SDL_Delay(10);
    return true;
  }

  std::vector<XrCompositionLayerBaseHeader *> layers;
  XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  std::array<XrCompositionLayerProjectionView, 2> proj_views{};
  XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};

  if (vr.frame_state.shouldRender) {
    if (vr.world_this_frame && vr.eye_rendered[0] && vr.eye_rendered[1]) {
      const float a = std::atan(vr.eye_tan);
      for (int i = 0; i < 2; i++) {
        auto &pv = proj_views[i];
        pv = XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        pv.pose = vr.views[i].pose;
        pv.fov = {-a, a, a, -a};
        pv.subImage.swapchain = vr.eye[i].handle;
        pv.subImage.imageRect = {{0, 0}, {vr.eye_size, vr.eye_size}};
      }
      projection.space = vr.local_space;
      projection.viewCount = 2;
      projection.views = proj_views.data();
      layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&projection));
    }

    // The flat screen: copy the engine's screen framebuffer into its layer.
    if (EnsureScreenSwapchain(w, h) && AcquireInto(vr.screen, vr.screen_fbo)) {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, screen_fbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, vr.screen_fbo);
      glDisable(GL_SCISSOR_TEST);
      glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      Release(vr.screen);

      quad.space = vr.local_space;
      quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
      quad.subImage.swapchain = vr.screen.handle;
      quad.subImage.imageRect = {{0, 0}, {w, h}};
      quad.pose.orientation.w = 1;
      quad.pose.position = {0, 0, -kScreenDistance};
      quad.size = {kScreenWidth, kScreenWidth * static_cast<float>(h) / static_cast<float>(w)};
      // Over the world the layer is premultiplied-alpha; alone it is opaque.
      quad.layerFlags = vr.overlay_transparent ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT : 0;
      layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&quad));
    }
  }

  XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
  end.displayTime = vr.frame_state.predictedDisplayTime;
  end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end.layerCount = static_cast<uint32_t>(layers.size());
  end.layers = layers.data();
  XrOk(xrEndFrame(vr.session, &end), "xrEndFrame");
  vr.frame_begun = false;

  vr.world_last_frame = vr.world_this_frame;

  vr.stat_frames++;
  vr.stat_stereo += vr.world_this_frame ? 1 : 0;
  if (vr.stat_start < 0) {
    vr.stat_start = vr.time;
  } else if (vr.time - vr.stat_start >= 5.0f) {
    LOG_INFO.printf("VR: %.1f fps (%d%% stereo)", vr.stat_frames / (vr.time - vr.stat_start),
                    100 * vr.stat_stereo / vr.stat_frames);
    vr.stat_frames = vr.stat_stereo = 0;
    vr.stat_start = vr.time;
  }
  if (vr.overlay_transparent) {
    vr.overlay_transparent = false;
    opengl_InvalidateBlendState();
  }
  opengl_BindScreenFramebuffer();

  // Pace the engine to the headset and read the controllers for the next frame.
  BeginFrame();
  return true;
}

// ---------------------------------------------------------------------------
// Game-facing API (d3vr.h)

bool vr_IsActive() { return vr.initialized && vr.session_running; }

bool vr_BeginEyePass(int eye, vr_eye_view *view) {
  if (!vr.initialized || eye < 0 || eye > 1 || vr.active_eye >= 0) {
    return false;
  }
  if (!BeginFrame() || !vr.frame_state.shouldRender || !LocateViews()) {
    return false;
  }
  if (!AcquireInto(vr.eye[eye], vr.eye_fbo)) {
    return false;
  }
  ClearBound(vr.eye_size, vr.eye_size, 0, 0, 0, 1);

  vr.active_eye = eye;
  vr.saved_screen_w = gpu_state.screen_width;
  vr.saved_screen_h = gpu_state.screen_height;
  gpu_state.screen_width = vr.eye_size;
  gpu_state.screen_height = vr.eye_size;
  // The renderer caches the 2D (pass-through) transform, which depends on
  // the screen size; make it recompute for the eye target and back.
  g3_ForceTransformRefresh();

  const XrPosef &pose = vr.views[eye].pose;
  view->size = vr.eye_size;
  view->zoom = vr.eye_tan / 0.75f; // g3_StartFrame: vertical tan = zoom * 3/4 (square viewport)
  ToDescent(pose.position, vr.world_scale, view->offset);
  ToDescent(Rotate(pose.orientation, {1, 0, 0}), 1.0f, view->right);
  ToDescent(Rotate(pose.orientation, {0, 1, 0}), 1.0f, view->up);
  ToDescent(Rotate(pose.orientation, {0, 0, -1}), 1.0f, view->forward);
  return true;
}

void vr_EndEyePass() {
  if (vr.active_eye < 0) {
    return;
  }
  Release(vr.eye[vr.active_eye]);
  vr.eye_rendered[vr.active_eye] = true;
  gpu_state.screen_width = vr.saved_screen_w;
  gpu_state.screen_height = vr.saved_screen_h;
  vr.active_eye = -1;
  vr.world_this_frame = true;
  opengl_BindScreenFramebuffer();
  g3_ForceTransformRefresh();
}

void vr_BeginOverlay() {
  if (!vr.initialized) {
    return;
  }
  opengl_BindScreenFramebuffer();
  ClearBound(gpu_state.screen_width, gpu_state.screen_height, 0, 0, 0, 0);
  opengl_BindScreenFramebuffer(); // restores the engine's viewport
  vr.overlay_transparent = true;
  opengl_InvalidateBlendState();
  g3_ForceTransformRefresh();
}

bool vr_GetControllerState(vr_controller_state *state) {
  if (!vr_IsActive() || vr.session_state != XR_SESSION_STATE_FOCUSED) {
    return false;
  }
  BeginFrame();
  *state = vr.input.state;
  return true;
}

#endif // D3_OPENXR
