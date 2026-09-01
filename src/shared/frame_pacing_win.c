#include "frame_pacing_win.h"

#include <math.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif

void gd_frame_pacer_init(GdFramePacer *pacer, double fps) {
    if (!pacer) return;
    memset(pacer, 0, sizeof(*pacer));
    if (!(fps > 0.0) || !isfinite(fps)) return;
#ifdef _WIN32
    {
        LARGE_INTEGER frequency;
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return;
        pacer->frequency = (long long)frequency.QuadPart;
    }
    pacer->fps = fps;
    pacer->initialized = 1;
    pacer->timer_period_1ms = timeBeginPeriod(1u) == TIMERR_NOERROR;
#else
    (void)fps;
#endif
}

void gd_frame_pacer_reset(GdFramePacer *pacer) {
    if (!pacer) return;
#ifdef _WIN32
    pacer->next_deadline = 0;
#endif
}

void gd_frame_pacer_wait(GdFramePacer *pacer) {
#ifdef _WIN32
    LARGE_INTEGER now;
    double interval_ticks_d;
    LONGLONG interval_ticks;
    if (!pacer || !pacer->initialized || !(pacer->fps > 0.0)) return;
    interval_ticks_d = (double)pacer->frequency / pacer->fps;
    interval_ticks = (LONGLONG)(interval_ticks_d + 0.5);
    if (interval_ticks < 1) interval_ticks = 1;
    QueryPerformanceCounter(&now);
    if (!pacer->next_deadline ||
        now.QuadPart > (LONGLONG)pacer->next_deadline + interval_ticks * 3) {
        pacer->next_deadline = (long long)(now.QuadPart + interval_ticks);
    } else {
        pacer->next_deadline += (long long)interval_ticks;
    }
    for (;;) {
        double remaining_ms;
        QueryPerformanceCounter(&now);
        if (now.QuadPart >= (LONGLONG)pacer->next_deadline) break;
        remaining_ms = (double)((LONGLONG)pacer->next_deadline - now.QuadPart) * 1000.0 /
                       (double)pacer->frequency;
        if (remaining_ms > 2.0) {
            Sleep((DWORD)(remaining_ms - 1.0));
        } else {
            SwitchToThread();
        }
    }
#else
    (void)pacer;
#endif
}

void gd_frame_pacer_destroy(GdFramePacer *pacer) {
    if (!pacer) return;
#ifdef _WIN32
    if (pacer->timer_period_1ms) timeEndPeriod(1u);
#endif
    memset(pacer, 0, sizeof(*pacer));
}
