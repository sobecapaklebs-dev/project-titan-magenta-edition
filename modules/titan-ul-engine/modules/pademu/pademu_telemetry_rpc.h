#ifndef PADEMU_TELEMETRY_RPC_H
#define PADEMU_TELEMETRY_RPC_H
#include <titan_pademu_telemetry.h>
int pademu_telemetry_rpc_start(void);
void pademu_telemetry_rpc_stop(void);
void pademu_telemetry_system_button(unsigned int port, unsigned int device_type, int pressed);
void pademu_trace_record(unsigned int code, int result, unsigned int a, unsigned int b, unsigned int c);
#endif
