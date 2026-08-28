#ifndef TITAN_CONTROLLER_UX_H
#define TITAN_CONTROLLER_UX_H
#include <stdint.h>
typedef struct TitanControllerUxMetrics { uint32_t deferred,shown,duplicates_suppressed,disconnects; } TitanControllerUxMetrics;
void TitanControllerUx_Initialize(void);
void TitanControllerUx_Update(int home_ready);
void TitanControllerUx_Shutdown(void);
const TitanControllerUxMetrics *TitanControllerUx_GetMetrics(void);
#endif
