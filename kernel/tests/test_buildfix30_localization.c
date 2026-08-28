#include "titan_localization.h"
#include "titan_storage_io.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char g_saved[64];
static int g_open_read = 0;
int TitanStorageIo_Open(const char *path, int flags) { (void)path; if (flags & TITAN_STORAGE_IO_READ) return g_open_read ? 1 : -1; return 2; }
int TitanStorageIo_Close(int descriptor) { (void)descriptor; return 0; }
TitanStorageIoResult TitanStorageIo_ReadSome(int descriptor, void *buffer, size_t size) { size_t n; (void)descriptor; n=strlen(g_saved); if(n>size)n=size; memcpy(buffer,g_saved,n); return (TitanStorageIoResult)n; }
int TitanStorageIo_WriteExact(int descriptor, const void *buffer, size_t size) { (void)descriptor; if(size>=sizeof(g_saved))return 0; memcpy(g_saved,buffer,size); g_saved[size]='\0'; return 1; }
int TitanStorageIo_Flush(int descriptor) { (void)descriptor; return 0; }
int TitanStorageIo_CreateDirectory(const char *path) { (void)path; return 0; }
int TitanStorageIo_Remove(const char *path) { (void)path; return 0; }
int TitanStorageIo_AtomicReplace(const char *a,const char *b,const char *c){(void)a;(void)b;(void)c;return 0;}
void TitanStorageScheduler_NotifyBackgroundWork(void) {}

int main(void) {
    TitanLocalization_Initialize();
    assert(TitanLocalization_GetLanguage()==TITAN_LANGUAGE_ENGLISH);
    assert(strcmp(TitanLocalization_Get(TITAN_STR_SETTINGS),"Settings")==0);
    TitanLocalization_CycleLanguage(1);
    assert(TitanLocalization_GetLanguage()==TITAN_LANGUAGE_PORTUGUESE);
    assert(strcmp(TitanLocalization_Get(TITAN_STR_SETTINGS),"Configurações")==0);
    assert(TitanLocalization_HasStorageWork());
    assert(TitanLocalization_StorageWorkerPump()==1);
    assert(strstr(g_saved,"language=pt-BR")!=0);
    TitanLocalization_CycleLanguage(1);
    assert(strcmp(TitanLocalization_GetLanguageName(),"Español")==0);
    TitanLocalization_CycleLanguage(1);
    assert(TitanLocalization_GetLanguage()==TITAN_LANGUAGE_ENGLISH);
    TitanLocalization_CycleLanguage(-1);
    assert(TitanLocalization_GetLanguage()==TITAN_LANGUAGE_SPANISH);
    puts("buildfix30-localization-host: OK");
    return 0;
}
