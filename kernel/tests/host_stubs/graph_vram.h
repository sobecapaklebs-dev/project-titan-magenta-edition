#ifndef TITAN_HOST_STUB_GRAPH_VRAM_H
#define TITAN_HOST_STUB_GRAPH_VRAM_H
#define GRAPH_ALIGN_BLOCK 0
#define GRAPH_ALIGN_PAGE 1
void graph_vram_clear(void);
int graph_vram_allocate(unsigned int, unsigned int, int, int);
void graph_vram_free(int);
#endif
