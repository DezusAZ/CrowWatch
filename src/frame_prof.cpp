#include "frame_prof.h"

namespace FrameProf {

static uint32_t s_acc[N];   // this frame
static uint32_t s_avg[N];   // smoothed, 1/8 a frame, like the totals in main.cpp
static uint32_t s_last;

void begin() {
    s_last = micros();
    for (uint8_t i = 0; i < N; i++) s_acc[i] = 0;
}

void lap(Slot s) {
    const uint32_t t = micros();
    s_acc[s] += t - s_last;
    s_last = t;
}

void endFrame() {
    for (uint8_t i = 0; i < N; i++)
        s_avg[i] = s_avg[i] ? s_avg[i] + ((int32_t)s_acc[i] - (int32_t)s_avg[i]) / 8 : s_acc[i];
}

void print() {
    static const char* NAMES[N] = { "pre", "bg", "squachy", "idle", "headline", "chrome", "post", "push", "x1", "x2", "x3", "x4", "x5", "x6" };
    Serial.print("[frame]");
    for (uint8_t i = 0; i < N; i++)
        if (i < X1 || s_avg[i])
        Serial.printf("  %s %lu.%lu", NAMES[i],
                      (unsigned long)(s_avg[i] / 1000), (unsigned long)((s_avg[i] / 100) % 10));
    Serial.println();
}

}  // namespace FrameProf
