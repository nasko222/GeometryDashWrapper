#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "antialias_win.h"

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif

#define WGL_DRAW_TO_WINDOW_ARB 0x2001
#define WGL_SUPPORT_OPENGL_ARB 0x2010
#define WGL_DOUBLE_BUFFER_ARB 0x2011
#define WGL_PIXEL_TYPE_ARB 0x2013
#define WGL_TYPE_RGBA_ARB 0x202B
#define WGL_COLOR_BITS_ARB 0x2014
#define WGL_DEPTH_BITS_ARB 0x2022
#define WGL_STENCIL_BITS_ARB 0x2023
#define WGL_SAMPLE_BUFFERS_ARB 0x2041
#define WGL_SAMPLES_ARB 0x2042

typedef BOOL (WINAPI *GdWglChoosePixelFormatArb)(HDC, const int *,
                                                  const FLOAT *, UINT,
                                                  int *, UINT *);

typedef GLuint (APIENTRY *GdGlCreateShader)(GLenum);
typedef void (APIENTRY *GdGlShaderSource)(GLuint, GLsizei,
                                          const char *const *, const GLint *);
typedef void (APIENTRY *GdGlCompileShader)(GLuint);
typedef void (APIENTRY *GdGlGetShaderiv)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *GdGlDeleteShader)(GLuint);
typedef GLuint (APIENTRY *GdGlCreateProgram)(void);
typedef void (APIENTRY *GdGlAttachShader)(GLuint, GLuint);
typedef void (APIENTRY *GdGlLinkProgram)(GLuint);
typedef void (APIENTRY *GdGlGetProgramiv)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *GdGlDeleteProgram)(GLuint);
typedef void (APIENTRY *GdGlUseProgram)(GLuint);
typedef GLint (APIENTRY *GdGlGetUniformLocation)(GLuint, const char *);
typedef void (APIENTRY *GdGlUniform1i)(GLint, GLint);
typedef void (APIENTRY *GdGlUniform2f)(GLint, GLfloat, GLfloat);

typedef struct GdFxaaState {
    int attempted;
    int ready;
    GLuint texture;
    int width;
    int height;
    GLuint program;
    GdGlDeleteShader delete_shader;
    GdGlDeleteProgram delete_program;
    GdGlUseProgram use_program;
    GdGlGetUniformLocation get_uniform_location;
    GdGlUniform1i uniform1i;
    GdGlUniform2f uniform2f;
    GLint texture_uniform;
    GLint inverse_size_uniform;
} GdFxaaState;

static GdFxaaState g_fxaa;

static int gd_valid_wgl_proc(const void *pointer) {
    const uintptr_t value = (uintptr_t)pointer;
    return pointer && value != 1u && value != 2u && value != 3u &&
           value != (uintptr_t)-1;
}

static void *gd_gl_proc(const char *name) {
    void *pointer = (void *)wglGetProcAddress(name);
    if (gd_valid_wgl_proc(pointer)) return pointer;
    {
        HMODULE library = GetModuleHandleA("opengl32.dll");
        if (!library) library = LoadLibraryA("opengl32.dll");
        return library ? (void *)GetProcAddress(library, name) : NULL;
    }
}

static int gd_try_msaa_format(HDC target, const PIXELFORMATDESCRIPTOR *base,
                              int samples) {
    static const char class_name[] = "GDWrapperMsaaProbeWindow";
    WNDCLASSA wc;
    HWND window = NULL;
    HDC dc = NULL;
    HGLRC context = NULL;
    PIXELFORMATDESCRIPTOR probe;
    int basic_format = 0;
    int chosen = 0;
    UINT count = 0;
    GdWglChoosePixelFormatArb choose_arb = NULL;
    int attributes[32];
    int n = 0;

    memset(&wc, 0, sizeof(wc));
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = class_name;
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 0;
    window = CreateWindowExA(0, class_name, "", WS_POPUP, 0, 0, 1, 1,
                             NULL, NULL, wc.hInstance, NULL);
    if (!window) return 0;
    dc = GetDC(window);
    if (!dc) goto finished;
    probe = *base;
    basic_format = ChoosePixelFormat(dc, &probe);
    if (!basic_format || !SetPixelFormat(dc, basic_format, &probe)) goto finished;
    context = wglCreateContext(dc);
    if (!context || !wglMakeCurrent(dc, context)) goto finished;
    choose_arb = (GdWglChoosePixelFormatArb)wglGetProcAddress(
        "wglChoosePixelFormatARB");
    if (!gd_valid_wgl_proc((const void *)choose_arb)) goto finished;

#define GD_WGL_ATTR(k, v) do { attributes[n++] = (k); attributes[n++] = (v); } while (0)
    GD_WGL_ATTR(WGL_DRAW_TO_WINDOW_ARB, TRUE);
    GD_WGL_ATTR(WGL_SUPPORT_OPENGL_ARB, TRUE);
    GD_WGL_ATTR(WGL_DOUBLE_BUFFER_ARB, TRUE);
    GD_WGL_ATTR(WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB);
    GD_WGL_ATTR(WGL_COLOR_BITS_ARB, base->cColorBits ? base->cColorBits : 32);
    GD_WGL_ATTR(WGL_DEPTH_BITS_ARB, base->cDepthBits ? base->cDepthBits : 24);
    GD_WGL_ATTR(WGL_STENCIL_BITS_ARB, base->cStencilBits);
    GD_WGL_ATTR(WGL_SAMPLE_BUFFERS_ARB, 1);
    GD_WGL_ATTR(WGL_SAMPLES_ARB, samples);
    attributes[n++] = 0;
#undef GD_WGL_ATTR

    if (!choose_arb(target, attributes, NULL, 1, &chosen, &count) ||
        !count || !chosen) chosen = 0;

finished:
    wglMakeCurrent(NULL, NULL);
    if (context) wglDeleteContext(context);
    if (dc && window) ReleaseDC(window, dc);
    if (window) DestroyWindow(window);
    return chosen;
}

int gd_gl_choose_pixel_format(HDC device,
                              const PIXELFORMATDESCRIPTOR *base_descriptor,
                              int requested_samples,
                              int *actual_samples) {
    PIXELFORMATDESCRIPTOR descriptor;
    int requested;
    int format = 0;
    if (actual_samples) *actual_samples = 0;
    if (!device || !base_descriptor) return 0;
    descriptor = *base_descriptor;
    requested = requested_samples;
    if (requested > 8) requested = 8;
    if (requested > 0 && requested < 2) requested = 2;
    while (requested >= 2 && !format) {
        format = gd_try_msaa_format(device, &descriptor, requested);
        if (format) {
            if (actual_samples) *actual_samples = requested;
            break;
        }
        requested = requested >= 8 ? 4 : requested >= 4 ? 2 : 0;
    }
    if (!format) format = ChoosePixelFormat(device, &descriptor);
    return format;
}

static GLuint gd_compile_shader(GLenum type, const char *source,
                                GdGlCreateShader create_shader,
                                GdGlShaderSource shader_source,
                                GdGlCompileShader compile_shader,
                                GdGlGetShaderiv get_shader_iv) {
    GLuint shader;
    GLint okay = 0;
    shader = create_shader(type);
    if (!shader) return 0;
    shader_source(shader, 1, &source, NULL);
    compile_shader(shader);
    get_shader_iv(shader, GL_COMPILE_STATUS, &okay);
    if (!okay) {
        if (g_fxaa.delete_shader) g_fxaa.delete_shader(shader);
        return 0;
    }
    return shader;
}

static int gd_fxaa_initialize(void) {
    static const char vertex_source[] =
        "#version 120\n"
        "varying vec2 v_uv;\n"
        "void main(){ gl_Position = gl_Vertex; v_uv = gl_MultiTexCoord0.xy; }\n";
    static const char fragment_source[] =
        "#version 120\n"
        "uniform sampler2D u_tex;\n"
        "uniform vec2 u_rcp;\n"
        "varying vec2 v_uv;\n"
        "void main(){\n"
        " vec3 nw=texture2D(u_tex,v_uv+vec2(-1.0,-1.0)*u_rcp).rgb;\n"
        " vec3 ne=texture2D(u_tex,v_uv+vec2( 1.0,-1.0)*u_rcp).rgb;\n"
        " vec3 sw=texture2D(u_tex,v_uv+vec2(-1.0, 1.0)*u_rcp).rgb;\n"
        " vec3 se=texture2D(u_tex,v_uv+vec2( 1.0, 1.0)*u_rcp).rgb;\n"
        " vec3 m =texture2D(u_tex,v_uv).rgb;\n"
        " vec3 l=vec3(0.299,0.587,0.114);\n"
        " float lnw=dot(nw,l),lne=dot(ne,l),lsw=dot(sw,l),lse=dot(se,l),lm=dot(m,l);\n"
        " float lmin=min(lm,min(min(lnw,lne),min(lsw,lse)));\n"
        " float lmax=max(lm,max(max(lnw,lne),max(lsw,lse)));\n"
        " vec2 d; d.x=-((lnw+lne)-(lsw+lse)); d.y=((lnw+lsw)-(lne+lse));\n"
        " float reduce=max((lnw+lne+lsw+lse)*(0.25*0.125),0.0078125);\n"
        " float inv=1.0/(min(abs(d.x),abs(d.y))+reduce);\n"
        " d=clamp(d*inv,vec2(-8.0),vec2(8.0))*u_rcp;\n"
        " vec3 a=0.5*(texture2D(u_tex,v_uv+d*(1.0/3.0-0.5)).rgb+texture2D(u_tex,v_uv+d*(2.0/3.0-0.5)).rgb);\n"
        " vec3 b=a*0.5+0.25*(texture2D(u_tex,v_uv+d*-0.5).rgb+texture2D(u_tex,v_uv+d*0.5).rgb);\n"
        " float lb=dot(b,l); gl_FragColor=vec4((lb<lmin||lb>lmax)?a:b,1.0);\n"
        "}\n";
    GdGlCreateShader create_shader;
    GdGlShaderSource shader_source;
    GdGlCompileShader compile_shader;
    GdGlGetShaderiv get_shader_iv;
    GdGlCreateProgram create_program;
    GdGlAttachShader attach_shader;
    GdGlLinkProgram link_program;
    GdGlGetProgramiv get_program_iv;
    GLuint vertex = 0;
    GLuint fragment = 0;
    GLint okay = 0;

    if (g_fxaa.attempted) return g_fxaa.ready;
    g_fxaa.attempted = 1;
    create_shader = (GdGlCreateShader)gd_gl_proc("glCreateShader");
    shader_source = (GdGlShaderSource)gd_gl_proc("glShaderSource");
    compile_shader = (GdGlCompileShader)gd_gl_proc("glCompileShader");
    get_shader_iv = (GdGlGetShaderiv)gd_gl_proc("glGetShaderiv");
    g_fxaa.delete_shader = (GdGlDeleteShader)gd_gl_proc("glDeleteShader");
    create_program = (GdGlCreateProgram)gd_gl_proc("glCreateProgram");
    attach_shader = (GdGlAttachShader)gd_gl_proc("glAttachShader");
    link_program = (GdGlLinkProgram)gd_gl_proc("glLinkProgram");
    get_program_iv = (GdGlGetProgramiv)gd_gl_proc("glGetProgramiv");
    g_fxaa.delete_program = (GdGlDeleteProgram)gd_gl_proc("glDeleteProgram");
    g_fxaa.use_program = (GdGlUseProgram)gd_gl_proc("glUseProgram");
    g_fxaa.get_uniform_location = (GdGlGetUniformLocation)gd_gl_proc("glGetUniformLocation");
    g_fxaa.uniform1i = (GdGlUniform1i)gd_gl_proc("glUniform1i");
    g_fxaa.uniform2f = (GdGlUniform2f)gd_gl_proc("glUniform2f");
    if (!create_shader || !shader_source || !compile_shader || !get_shader_iv ||
        !g_fxaa.delete_shader || !create_program || !attach_shader ||
        !link_program || !get_program_iv || !g_fxaa.delete_program ||
        !g_fxaa.use_program || !g_fxaa.get_uniform_location ||
        !g_fxaa.uniform1i || !g_fxaa.uniform2f) return 0;

    vertex = gd_compile_shader(GL_VERTEX_SHADER, vertex_source, create_shader,
                               shader_source, compile_shader, get_shader_iv);
    fragment = gd_compile_shader(GL_FRAGMENT_SHADER, fragment_source, create_shader,
                                 shader_source, compile_shader, get_shader_iv);
    if (!vertex || !fragment) goto fail;
    g_fxaa.program = create_program();
    if (!g_fxaa.program) goto fail;
    attach_shader(g_fxaa.program, vertex);
    attach_shader(g_fxaa.program, fragment);
    link_program(g_fxaa.program);
    get_program_iv(g_fxaa.program, GL_LINK_STATUS, &okay);
    if (!okay) goto fail;
    g_fxaa.texture_uniform = g_fxaa.get_uniform_location(g_fxaa.program, "u_tex");
    g_fxaa.inverse_size_uniform = g_fxaa.get_uniform_location(g_fxaa.program, "u_rcp");
    glGenTextures(1, &g_fxaa.texture);
    if (!g_fxaa.texture) goto fail;
    g_fxaa.delete_shader(vertex);
    g_fxaa.delete_shader(fragment);
    g_fxaa.ready = 1;
    return 1;

fail:
    if (vertex) g_fxaa.delete_shader(vertex);
    if (fragment) g_fxaa.delete_shader(fragment);
    if (g_fxaa.program && g_fxaa.delete_program) g_fxaa.delete_program(g_fxaa.program);
    g_fxaa.program = 0;
    return 0;
}

int gd_fxaa_apply(HWND window) {
    RECT rect;
    GLint old_program = 0;
    GLint old_texture = 0;
    int width;
    int height;
    if (!window || !gd_fxaa_initialize()) return 0;
    if (!GetClientRect(window, &rect)) return 0;
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return 0;

    glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    glBindTexture(GL_TEXTURE_2D, g_fxaa.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (g_fxaa.width != width || g_fxaa.height != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, NULL);
        g_fxaa.width = width;
        g_fxaa.height = height;
    }
    glReadBuffer(GL_BACK);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_TEXTURE_2D);
    glViewport(0, 0, width, height);
    g_fxaa.use_program(g_fxaa.program);
    if (g_fxaa.texture_uniform >= 0) g_fxaa.uniform1i(g_fxaa.texture_uniform, 0);
    if (g_fxaa.inverse_size_uniform >= 0)
        g_fxaa.uniform2f(g_fxaa.inverse_size_uniform,
                         1.0f / (GLfloat)width, 1.0f / (GLfloat)height);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();
    g_fxaa.use_program((GLuint)old_program);
    glPopAttrib();
    glBindTexture(GL_TEXTURE_2D, (GLuint)old_texture);
    return 1;
}

void gd_fxaa_shutdown(void) {
    if (g_fxaa.texture) glDeleteTextures(1, &g_fxaa.texture);
    if (g_fxaa.program && g_fxaa.delete_program)
        g_fxaa.delete_program(g_fxaa.program);
    memset(&g_fxaa, 0, sizeof(g_fxaa));
}
