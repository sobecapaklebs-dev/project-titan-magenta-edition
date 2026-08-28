#include <ultra64.h>
#include "chrobjdata.h"

void init_player_gait_object(void) {
#ifdef NATIVE_PORT
  player_gait_object_header.RootNode = &player_gait_hdr;
#else
  player_gait_object_header.RootNode = (int)&player_gait_hdr;
#endif
  return;
}

