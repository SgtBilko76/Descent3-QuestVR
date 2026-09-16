// Descent 3 - Meta Quest port.
// Compiles and links the engine's shaders against a real driver.
//
// The engine only builds its shaders during renderer init, which is far into
// startup (and unreachable without game data), so this checks them in
// isolation: the exact sources from the generated shaders.h, with the same
// preamble ShaderProgram.h prepends.
//
// Built two ways from this one file, since the .glsl sources are shared:
//   - D3_GLES: headless EGL pbuffer, GLES 3.2 (runs on the headset)
//   - else:    hidden SDL window, desktop GL 3.2 core (runs on the build host)
//
//   quest/tools/check_shaders.sh [--device|--host|--all]
#if defined(D3_GLES)
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#else
#include <SDL3/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL_opengl.h>
#endif

#include <cstdio>
#include <string_view>

#include "ShaderPreamble.h"
#include "shaders.h"

namespace {

bool Compile(GLenum type, std::string_view name, std::string_view src, GLuint &out) {
  GLuint id = glCreateShader(type);
  const char *ptrs[] = {kShaderPreamble.data(), src.data()};
  GLint lens[] = {GLint(kShaderPreamble.size()), GLint(src.size())};
  glShaderSource(id, 2, ptrs, lens);
  glCompileShader(id);
  GLint ok = GL_FALSE;
  glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
  char log[4096] = {};
  glGetShaderInfoLog(id, sizeof(log), nullptr, log);
  std::printf("[%.*s] compile %s\n%s", int(name.size()), name.data(), ok ? "OK" : "FAILED", log);
  out = id;
  return ok == GL_TRUE;
}

} // namespace

#if defined(D3_GLES)
bool CreateContext() {
  EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(dpy, nullptr, nullptr)) {
    std::printf("eglInitialize failed: 0x%x\n", eglGetError());
    return false;
  }
  const EGLint cfg_attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                              EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
  EGLConfig cfg;
  EGLint ncfg = 0;
  if (!eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &ncfg) || ncfg < 1) {
    std::printf("eglChooseConfig failed: 0x%x\n", eglGetError());
    return false;
  }
  const EGLint pb_attrs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb_attrs);
  const EGLint ctx_attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
  if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, surf, surf, ctx)) {
    std::printf("context creation failed: 0x%x\n", eglGetError());
    return false;
  }
  return true;
}
#else
bool CreateContext() {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::printf("SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }
  // Same attributes the engine requests on desktop (HardwareOpenGL.cpp).
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_Window *win = SDL_CreateWindow("shader_check", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!win || !SDL_GL_CreateContext(win)) {
    std::printf("GL context creation failed: %s\n", SDL_GetError());
    return false;
  }
  return true;
}
#endif

int main() {
  if (!CreateContext()) {
    return 2;
  }
  std::printf("GL_VERSION:  %s\nGL_RENDERER: %s\nGLSL:        %s\n\n", glGetString(GL_VERSION),
              glGetString(GL_RENDERER), glGetString(GL_SHADING_LANGUAGE_VERSION));

  GLuint vs = 0, fs = 0;
  bool ok = Compile(GL_VERTEX_SHADER, "vertex", shaders::vertex, vs);
  ok = Compile(GL_FRAGMENT_SHADER, "fragment", shaders::fragment, fs) && ok;
  if (ok) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    // Same attribute order the engine binds in VertexBuffer.
    glBindAttribLocation(prog, 0, "in_pos");
    glBindAttribLocation(prog, 1, "in_color");
    glBindAttribLocation(prog, 2, "in_uv0");
    glBindAttribLocation(prog, 3, "in_uv1");
    glLinkProgram(prog);
    GLint linked = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    char log[4096] = {};
    glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
    std::printf("[program] link %s\n%s", linked ? "OK" : "FAILED", log);
    ok = linked == GL_TRUE;
    if (ok) {
      // Every uniform the engine sets by name must survive linking.
      for (const char *u : {"u_modelview", "u_projection", "u_texture0", "u_texture1", "u_texture_enable",
                            "u_fog_enable", "u_fog_color", "u_fog_start", "u_fog_end", "u_gamma"}) {
        GLint loc = glGetUniformLocation(prog, u);
        std::printf("  uniform %-18s %s\n", u, loc >= 0 ? "ok" : "MISSING");
        ok = ok && loc >= 0;
      }
    }
  }
  std::printf("\nRESULT: %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
