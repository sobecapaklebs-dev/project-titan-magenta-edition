#include "project_nes_fceumm_compat.h"
#include "../include/titan_app_sdk.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <streams/file_stream.h>
#include <file/file_path.h>
#include <string/stdstring.h>
#include <compat/strl.h>

static const TitanAppApiV1 *g_titan_api;

void ProjectNesFceummCompat_BindApi(const TitanAppApiV1 *api) { g_titan_api=api; }
static const TitanAppApiV1 *ProjectNesFceumm_GetTitanApi(void) { return g_titan_api; }

typedef struct PnAllocHeader { uint32_t size; uint32_t align_pad[3]; } PnAllocHeader;

void *pn_fceumm_malloc(size_t size)
{
    const TitanAppApiV1 *api=ProjectNesFceumm_GetTitanApi();
    PnAllocHeader *h;
    if(!api || !api->alloc || size > 0x7ffffff0u) return 0;
    h=(PnAllocHeader*)api->alloc((uint32_t)size + (uint32_t)sizeof(*h));
    if(!h) return 0;
    h->size=(uint32_t)size;
    return h+1;
}
void pn_fceumm_free(void *ptr)
{
    const TitanAppApiV1 *api=ProjectNesFceumm_GetTitanApi();
    if(ptr && api && api->release) api->release(((PnAllocHeader*)ptr)-1);
}
void *pn_fceumm_calloc(size_t count,size_t size)
{
    size_t total; uint8_t *p; size_t i;
    if(size && count > ((size_t)-1)/size) return 0;
    total=count*size; p=(uint8_t*)pn_fceumm_malloc(total);
    if(p) for(i=0;i<total;i++) p[i]=0;
    return p;
}
void *pn_fceumm_realloc(void *ptr,size_t size)
{
    PnAllocHeader *oldh; uint8_t *out,*in; size_t n,i;
    if(!ptr) return pn_fceumm_malloc(size);
    if(!size){ pn_fceumm_free(ptr); return 0; }
    oldh=((PnAllocHeader*)ptr)-1; n=oldh->size<size?oldh->size:size;
    out=(uint8_t*)pn_fceumm_malloc(size); if(!out) return 0;
    in=(uint8_t*)ptr; for(i=0;i<n;i++) out[i]=in[i];
    pn_fceumm_free(ptr); return out;
}

size_t strlcpy_retro__(char *dst,const char *src,size_t size)
{
    size_t n=0,i; while(src[n]) n++;
    if(size){ size_t c=n < size-1 ? n : size-1; for(i=0;i<c;i++) dst[i]=src[i]; dst[c]=0; }
    return n;
}
size_t strlcat_retro__(char *dst,const char *src,size_t size)
{
    size_t d=0,s=0,i; while(d<size && dst[d]) d++; while(src[s]) s++;
    if(d==size) return size+s;
    for(i=0;i<s && d+i+1<size;i++) dst[d+i]=src[i];
    dst[d+i]=0;
    return d+s;
}
size_t fill_pathname_join(char *out,const char *dir,const char *path,size_t size)
{
    size_t n=strlcpy_retro__(out,dir,size);
    if(n && n<size && out[n-1]!='/') n=strlcat_retro__(out,"/",size);
    (void)n; return strlcat_retro__(out,path,size);
}
char *string_trim_whitespace(char *s)
{
    char *start=s,*end,*d; if(!s) return s;
    while(*start==' '||*start=='\t'||*start=='\r'||*start=='\n'||*start=='\f'||*start=='\v') start++;
    d=s; while(*start) *d++=*start++; *d=0; end=d;
    while(end>s && (end[-1]==' '||end[-1]=='\t'||end[-1]=='\r'||end[-1]=='\n'||end[-1]=='\f'||end[-1]=='\v')) --end;
    *end=0; return s;
}

struct RFILE { char path[1024]; int64_t pos; int64_t size; };
static int pn_path_copy(char *dst,const char *src,size_t cap)
{
    const char prefix[]="./"; (void)prefix;
    if(!src || !*src) return 0;
    while(src[0]=='.' && src[1]=='/') src+=2;
    return strlcpy_retro__(dst,src,cap)<cap;
}
static int64_t pn_measure(const char *path)
{
    const TitanAppApiV1 *api=ProjectNesFceumm_GetTitanApi(); uint8_t buf[512]; uint32_t off=0; int got;
    if(!api || !api->file_read) return -1;
    for(;;){ got=api->file_read(path,off,buf,sizeof(buf)); if(got<0) return -1; off+=(uint32_t)got; if(got<(int)sizeof(buf)) return off; }
}
void filestream_vfs_init(const struct retro_vfs_interface_info *info){ (void)info; }
RFILE *filestream_open(const char *path,unsigned mode,unsigned hints)
{
    const TitanAppApiV1 *api=ProjectNesFceumm_GetTitanApi(); RFILE *f; (void)hints;
    if(!api || !api->file_exists || !api->file_read || (mode & RETRO_VFS_FILE_ACCESS_WRITE)) return 0;
    f=(RFILE*)pn_fceumm_calloc(1,sizeof(*f)); if(!f || !pn_path_copy(f->path,path,sizeof(f->path)) || !api->file_exists(f->path)){ pn_fceumm_free(f); return 0; }
    f->size=pn_measure(f->path); if(f->size<0){ pn_fceumm_free(f); return 0; } return f;
}
int64_t filestream_get_size(RFILE *f){ return f?f->size:-1; }
int64_t filestream_tell(RFILE *f){ return f?f->pos:-1; }
int64_t filestream_read(RFILE *f,void *data,int64_t len)
{
    const TitanAppApiV1 *api=ProjectNesFceumm_GetTitanApi(); int got;
    if(!f||!api||!api->file_read||len<0||len>0x7fffffff) return -1;
    got=api->file_read(f->path,(uint32_t)f->pos,data,(uint32_t)len); if(got<0) return -1; f->pos+=got; return got;
}
int64_t filestream_seek(RFILE *f,int64_t off,int whence)
{
    int64_t base,next; if(!f) return -1;
    base=whence==RETRO_VFS_SEEK_POSITION_START?0:whence==RETRO_VFS_SEEK_POSITION_CURRENT?f->pos:whence==RETRO_VFS_SEEK_POSITION_END?f->size:-1;
    if(base<0) return -1;
    next=base+off;
    if(next<0) return -1;
    f->pos=next;
    return f->pos;
}
int filestream_close(RFILE *f){ pn_fceumm_free(f); return 0; }
bool filestream_exists(const char *path){ const TitanAppApiV1 *api=ProjectNesFceumm_GetTitanApi(); char p[1024]; return api&&api->file_exists&&pn_path_copy(p,path,sizeof(p))&&api->file_exists(p); }
bool path_is_valid(const char *path){ return filestream_exists(path); }
