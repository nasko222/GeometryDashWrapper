#ifndef GD_FRAME_PACING_WIN_H
#define GD_FRAME_PACING_WIN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GdFramePacer {
    long long frequency;
    long long next_deadline;
    double fps;
    int initialized;
    int timer_period_1ms;
} GdFramePacer;

/* fps <= 0 disables host-side capping (used for FPS=VSYNC). */
void gd_frame_pacer_init(GdFramePacer *pacer, double fps);
void gd_frame_pacer_reset(GdFramePacer *pacer);
void gd_frame_pacer_wait(GdFramePacer *pacer);
void gd_frame_pacer_destroy(GdFramePacer *pacer);

#ifdef __cplusplus
}
#endif

#endif
