#include "titan_shell_busy.h"

#include <assert.h>

static void TestShellBusy_Pump(void *user_data)
{
    unsigned int *count;

    count = (unsigned int *)user_data;
    (*count)++;
}

int main(void)
{
    unsigned int pump_count;

    pump_count = 0u;
    TitanShellBusy_Initialize(TestShellBusy_Pump, &pump_count);
    assert(!TitanShellBusy_IsActive());

    TitanShellBusy_Pulse();
    assert(pump_count == 0u);

    TitanShellBusy_Begin();
    assert(TitanShellBusy_IsActive());
    TitanShellBusy_Pulse();
    assert(pump_count == 1u);

    TitanShellBusy_Begin();
    TitanShellBusy_End();
    assert(TitanShellBusy_IsActive());

    TitanShellBusy_End();
    assert(!TitanShellBusy_IsActive());
    TitanShellBusy_End();
    assert(!TitanShellBusy_IsActive());

    return 0;
}
