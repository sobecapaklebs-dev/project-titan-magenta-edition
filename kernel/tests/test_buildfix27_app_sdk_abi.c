#include "titan_app_sdk.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

/* Legacy tail layout frozen at CP05/CP10R. We only need its size boundary:
 * the first CP15 field must begin exactly after text_input_poll. */
typedef struct TitanAppApiV1LegacyTail
{
    uint32_t abi_version;
    uint32_t struct_size;
    void (*request_exit)(void);
    uint64_t (*uptime_ms)(void);
    void *(*alloc)(uint32_t);
    void (*release)(void *);
    void (*draw_text)(float,float,float,const char*,uint8_t,uint8_t,uint8_t,uint8_t);
    void (*draw_text_shadow)(float,float,float,const char*,uint8_t,uint8_t,uint8_t,uint8_t);
    void (*draw_rect)(float,float,float,float,uint8_t,uint8_t,uint8_t,uint8_t);
    void (*draw_panel)(float,float,float,float,uint8_t);
    void (*draw_progress)(float,float,float,float,uint32_t,uint8_t);
    int (*notify)(TitanAppNotificationKind,const char*,const char*);
    int (*file_read)(const char*,uint32_t,void*,uint32_t);
    int (*file_write_data)(const char*,const void*,uint32_t);
    int (*file_exists)(const char*);
    int (*media_begin_audio)(void);
    uint32_t (*media_submit_pcm)(const int16_t*,uint32_t);
    uint32_t (*media_audio_free_frames)(void);
    void (*media_end_audio)(void);
    int (*media_present_rgb565)(const uint16_t*,uint32_t,uint32_t,uint32_t);
    void (*media_draw)(float,float,float,float,uint8_t);
    int (*storage_list)(const char*,uint32_t,TitanAppStorageEntry*,uint32_t);
    int (*storage_stat)(const char*,TitanAppStorageEntry*);
    int (*storage_read)(const char*,uint64_t,void*,uint32_t);
    int (*storage_write)(const char*,uint64_t,const void*,uint32_t,uint32_t);
    int (*storage_mkdir)(const char*);
    int (*storage_remove)(const char*);
    int (*storage_rename)(const char*,const char*);
    int (*launch_elf)(const char*);
    int (*game_begin)(const struct TitanGameSessionConfig*,const struct TitanGameApiV1**);
    void (*game_end)(void);
    int (*storage_list_volumes)(uint32_t,TitanAppStorageVolume*,uint32_t);
    int (*storage_commit_replace)(const char*,const char*);
    int (*text_input_open)(const char*,const char*,uint32_t);
    int (*text_input_poll)(char*,uint32_t);
} TitanAppApiV1LegacyTail;

int main(void)
{
    assert(offsetof(TitanAppApiV1, network_get_begin) == sizeof(TitanAppApiV1LegacyTail));
    assert(offsetof(TitanAppApiV1, network_get_status) > offsetof(TitanAppApiV1, network_get_begin));
    assert(offsetof(TitanAppApiV1, network_get_read) > offsetof(TitanAppApiV1, network_get_status));
    assert(offsetof(TitanAppApiV1, network_cancel) > offsetof(TitanAppApiV1, network_get_read));
    puts("buildfix27-app-sdk-abi: OK legacy tail unchanged, network API append-only");
    return 0;
}
