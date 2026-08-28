#ifndef TITAN_HOST_STUB_GRAPH_H
#define TITAN_HOST_STUB_GRAPH_H

#define GRAPH_MODE_NTSC 2
#define GRAPH_MODE_PAL 3
#define GRAPH_MODE_HDTV_480P 80
#define GRAPH_MODE_HDTV_576P 81
#define GRAPH_MODE_HDTV_720P 82
#define GRAPH_MODE_INTERLACED 1
#define GRAPH_MODE_NONINTERLACED 0
#define GRAPH_MODE_FIELD 0
#define GRAPH_MODE_FRAME 1
#define GRAPH_ENABLE 1
#define GRAPH_DISABLE 0

int graph_get_region(void);
int graph_set_mode(int interlace, int mode, int ffmd, int flicker_filter);
void graph_disable_output(void);
void graph_enable_output(void);
void graph_set_screen(int x, int y, int width, int height);
void graph_set_bgcolor(unsigned char red, unsigned char green, unsigned char blue);
void graph_set_framebuffer_filtered(int fbp, int width, int psm, int x, int y);
void graph_set_framebuffer(int circuit, int fbp, int width, int psm, int x, int y);
void graph_wait_vsync(void);

#endif
