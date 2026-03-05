#include "../include/gomoku.h"

void launchTimer(timer *t)
{
    if (!t) return;
    if (!t->running)
    {
        clock_gettime(CLOCK_MONOTONIC, &t->start_ts);
        t->running = true;
    }
}

/* Dead code removed: stopTimer */

void resetTimer(timer *t)
{
    if (!t) return;
    t->running = false;
    t->elapsed = 0.0;
    t->start_ts.tv_sec = 0;
    t->start_ts.tv_nsec = 0;
}