#ifndef PROJECT_TITAN_SHELL_BUSY_H
#define PROJECT_TITAN_SHELL_BUSY_H

typedef void (*TitanShellBusyPumpCallback)(void *user_data);

void TitanShellBusy_Initialize(
    TitanShellBusyPumpCallback callback,
    void *user_data
);

/* Optional pump executed only when a background EE worker reaches a
 * cooperative storage yield. It must never render or touch UI state. */
void TitanShellBusy_SetWorkerPump(
    TitanShellBusyPumpCallback callback,
    void *user_data
);

void TitanShellBusy_Begin(void);
void TitanShellBusy_End(void);
void TitanShellBusy_Pulse(void);
int TitanShellBusy_IsActive(void);

#endif
