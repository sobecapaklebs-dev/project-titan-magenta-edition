#ifndef _MINIMAP_OVERLAY_H_
#define _MINIMAP_OVERLAY_H_

void minimap_overlay_draw_queued_frames(void);
#ifdef __APPLE__
void minimap_overlay_draw_queued_frames_metal(int fb_width, int fb_height);
#endif

#endif
