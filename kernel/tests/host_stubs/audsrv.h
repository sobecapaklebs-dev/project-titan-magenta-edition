#ifndef TITAN_HOST_STUB_AUDSRV_H
#define TITAN_HOST_STUB_AUDSRV_H

#define AUDSRV_ERR_NOERROR 0
#define MAX_VOLUME 0x3fff

typedef struct audsrv_fmt_t
{
    int freq;
    int bits;
    int channels;
} audsrv_fmt_t;

int audsrv_init(void);
int audsrv_quit(void);
int audsrv_set_format(audsrv_fmt_t *format);
int audsrv_set_volume(int volume);
int audsrv_stop_audio(void);
int audsrv_available(void);
int audsrv_play_audio(const char *chunk, int bytes);
int audsrv_queued(void);

#endif
