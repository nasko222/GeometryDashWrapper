#ifndef GD_ANTIALIAS_WIN_H
#define GD_ANTIALIAS_WIN_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Chooses an OpenGL pixel format. requested_samples may be 0, 2, 4, or 8.
   Falls back to the ordinary non-MSAA format when WGL_ARB_multisample is
   unavailable. Returns the pixel-format index and reports the actual sample
   count through actual_samples. */
int gd_gl_choose_pixel_format(HDC device,
                              const PIXELFORMATDESCRIPTOR *base_descriptor,
                              int requested_samples,
                              int *actual_samples);

/* Applies a lightweight GLSL 1.20 FXAA pass to the current back buffer.
   Returns 1 when the pass ran, 0 when unavailable/disabled for this context. */
int gd_fxaa_apply(HWND window);
void gd_fxaa_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
