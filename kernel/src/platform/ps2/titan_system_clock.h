#ifndef PROJECT_TITAN_SYSTEM_CLOCK_H
#define PROJECT_TITAN_SYSTEM_CLOCK_H

void TitanSystemClock_Initialize(void);

void TitanSystemClock_Update(
    unsigned int frame,
    int iop_services_ready
);

const char *TitanSystemClock_GetText(void);

int TitanSystemClock_IsValid(void);
int TitanSystemClock_GetHour24(void);

#endif
