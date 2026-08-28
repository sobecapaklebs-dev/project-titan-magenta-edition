#include "titan_shell_busy.h"

#if defined(__EE__) || defined(_EE)
#include <delaythread.h>
#include <kernel.h>
#endif

typedef struct TitanShellBusyState
{
    TitanShellBusyPumpCallback callback;
    void *user_data;
    unsigned int depth;
    int pumping;
    TitanShellBusyPumpCallback worker_callback;
    void *worker_user_data;
    int worker_pumping;
#if defined(__EE__) || defined(_EE)
    int owner_thread_id;
#endif
} TitanShellBusyState;

static TitanShellBusyState g_shell_busy;

void TitanShellBusy_Initialize(
    TitanShellBusyPumpCallback callback,
    void *user_data
)
{
    g_shell_busy.callback = callback;
    g_shell_busy.user_data = user_data;
    g_shell_busy.depth = 0u;
    g_shell_busy.pumping = 0;
    g_shell_busy.worker_callback = 0;
    g_shell_busy.worker_user_data = 0;
    g_shell_busy.worker_pumping = 0;
#if defined(__EE__) || defined(_EE)
    g_shell_busy.owner_thread_id = GetThreadId();
#endif
}


void TitanShellBusy_SetWorkerPump(
    TitanShellBusyPumpCallback callback,
    void *user_data
)
{
    g_shell_busy.worker_callback = callback;
    g_shell_busy.worker_user_data = user_data;
}

void TitanShellBusy_Begin(void)
{
    g_shell_busy.depth++;
}

void TitanShellBusy_End(void)
{
    if (g_shell_busy.depth > 0u)
    {
        g_shell_busy.depth--;
    }
}

void TitanShellBusy_Pulse(void)
{
#if defined(__EE__) || defined(_EE)
    /* Storage jobs run on a low-priority EE worker. They must yield, never
     * execute the render callback from the worker thread. */
    if (GetThreadId() != g_shell_busy.owner_thread_id)
    {
        /* Background storage never renders.  It may, however, use this
         * cooperative boundary to refill realtime audio from the same
         * serialized fileXio lane before yielding the EE. */
        if (g_shell_busy.worker_callback != 0 &&
            !g_shell_busy.worker_pumping)
        {
            g_shell_busy.worker_pumping = 1;
            g_shell_busy.worker_callback(g_shell_busy.worker_user_data);
            g_shell_busy.worker_pumping = 0;
        }
        DelayThread(4000);
        return;
    }
#endif
    if (
        g_shell_busy.depth == 0u ||
        g_shell_busy.callback == 0 ||
        g_shell_busy.pumping
    )
    {
        return;
    }

    g_shell_busy.pumping = 1;
    g_shell_busy.callback(g_shell_busy.user_data);
    g_shell_busy.pumping = 0;
}

int TitanShellBusy_IsActive(void)
{
    return g_shell_busy.depth > 0u;
}
