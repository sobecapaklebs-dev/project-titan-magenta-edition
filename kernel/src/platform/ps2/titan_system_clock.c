#include "titan_system_clock.h"

#include "titan_datetime.h"
#include <stdio.h>
#include <string.h>

typedef struct TitanSystemClockState {
    int valid;
    int hour24;
    char text[12];
} TitanSystemClockState;

static TitanSystemClockState g_clock;

void TitanSystemClock_Initialize(void)
{
    memset(&g_clock, 0, sizeof(g_clock));
    strcpy(g_clock.text, "--:--");
    g_clock.hour24 = -1;
}

void TitanSystemClock_Update(unsigned int frame, int iop_services_ready)
{
    const TitanDateTimeSnapshot *snapshot;
    int y,m,d,h,minute,second;
    (void)frame;
    if (!iop_services_ready) return;
    snapshot = TitanDateTime_GetSnapshot();
    if (!snapshot || !snapshot->valid) { g_clock.valid = 0; g_clock.hour24 = -1; strcpy(g_clock.text,"--:--"); return; }
    snprintf(g_clock.text,sizeof(g_clock.text),"%s",snapshot->time_text);
    if (TitanDateTime_UnixToCivil(snapshot->local_epoch_seconds,&y,&m,&d,&h,&minute,&second)) { g_clock.hour24=h; g_clock.valid=1; }
}

const char *TitanSystemClock_GetText(void) { return g_clock.text; }
int TitanSystemClock_IsValid(void) { return g_clock.valid; }
int TitanSystemClock_GetHour24(void) { return g_clock.valid ? g_clock.hour24 : -1; }
