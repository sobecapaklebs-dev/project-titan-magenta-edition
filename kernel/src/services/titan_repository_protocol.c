#include "titan_repository_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITAN_REPOSITORY_LINE_CAPACITY 768u

static int copy_text(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (!dst || cap == 0u || !src) return 0;
    n = strlen(src);
    if (n + 1u > cap) return 0;
    memcpy(dst, src, n + 1u);
    return 1;
}

static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t') s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
    return s;
}

static int safe_token(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!s || !*s) return 0;
    while (*p)
    {
        if (!(isalnum(*p) || *p == '.' || *p == '_' || *p == '-')) return 0;
        p++;
    }
    return 1;
}

static int safe_text(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!s) return 0;
    while (*p)
    {
        if (*p < 0x20u || *p == 0x7fu) return 0;
        p++;
    }
    return 1;
}

static int valid_url(const char *s)
{
    const char *p;
    if (!s) return 0;
    if (strncmp(s, "https://", 8u) != 0 && strncmp(s, "http://", 7u) != 0) return 0;
    p = strstr(s, "://") + 3;
    if (!*p) return 0;
    while (*p)
    {
        unsigned char c = (unsigned char)*p++;
        if (c <= 0x20u || c == 0x7fu || c == '\\' || c == '\r' || c == '\n') return 0;
    }
    return 1;
}

static int valid_sha256(const char *s)
{
    unsigned int i;
    if (!s || strlen(s) != 64u) return 0;
    for (i = 0u; i < 64u; i++) if (!isxdigit((unsigned char)s[i])) return 0;
    return 1;
}

static int package_complete(const TitanRepositoryPackage *p)
{
    return p && safe_token(p->package_id) && p->title[0] && p->version[0] &&
        strcmp(p->platform, TITAN_REPOSITORY_PLATFORM_PS2) == 0 &&
        strcmp(p->architecture, TITAN_REPOSITORY_ARCH_R5900) == 0 &&
        valid_url(p->download_url) && valid_sha256(p->sha256) && p->package_size > 0u &&
        (!p->icon_url[0] || valid_url(p->icon_url)) &&
        (!p->background_url[0] || valid_url(p->background_url));
}

static int set_field(TitanRepositoryPackage *p, const char *key, const char *value)
{
    char *end = 0;
    unsigned long long n;
    if (!strcmp(key,"id")) return copy_text(p->package_id,sizeof(p->package_id),value);
    if (!strcmp(key,"title")) return safe_text(value) && copy_text(p->title,sizeof(p->title),value);
    if (!strcmp(key,"version")) return safe_token(value) && copy_text(p->version,sizeof(p->version),value);
    if (!strcmp(key,"platform")) return copy_text(p->platform,sizeof(p->platform),value);
    if (!strcmp(key,"architecture")) return copy_text(p->architecture,sizeof(p->architecture),value);
    if (!strcmp(key,"url")) return copy_text(p->download_url,sizeof(p->download_url),value);
    if (!strcmp(key,"sha256")) return copy_text(p->sha256,sizeof(p->sha256),value);
    if (!strcmp(key,"size"))
    {
        n = strtoull(value,&end,10);
        if (end == value || *end != '\0' || n == 0u) return 0;
        p->package_size = n; return 1;
    }
    if (!strcmp(key,"description")) return safe_text(value) && copy_text(p->description,sizeof(p->description),value);
    if (!strcmp(key,"icon")) return copy_text(p->icon_url,sizeof(p->icon_url),value);
    if (!strcmp(key,"background")) return copy_text(p->background_url,sizeof(p->background_url),value);
    if (!strcmp(key,"api")) return safe_token(value) && copy_text(p->api_level,sizeof(p->api_level),value);
    if (!strcmp(key,"dependencies")) return safe_text(value) && copy_text(p->dependencies,sizeof(p->dependencies),value);
    return 0;
}

const TitanRepositoryPackage *TitanRepository_FindPackage(const TitanRepositoryCatalog *c,const char *id)
{
    unsigned int i;
    if (!c || !id) return 0;
    for (i=0u;i<c->package_count;i++) if (!strcmp(c->packages[i].package_id,id)) return &c->packages[i];
    return 0;
}

TitanRepositoryParseResult TitanRepository_Parse(const char *text,size_t size,TitanRepositoryCatalog *out)
{
    size_t off=0u; unsigned int line_no=0u; TitanRepositoryPackage *current=0;
    char line[TITAN_REPOSITORY_LINE_CAPACITY];
    if (!text || !out || size==0u) return TITAN_REPOSITORY_PARSE_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    while (off<size)
    {
        size_t used=0u; char *s,*eq;
        while (off<size && text[off]!='\n')
        {
            if (used+1u>=sizeof(line)) return TITAN_REPOSITORY_PARSE_BAD_SYNTAX;
            if (text[off]!='\r') line[used++]=text[off];
            off++;
        }
        if (off<size && text[off]=='\n') off++;
        line[used]='\0'; line_no++; s=trim(line);
        if (line_no==1u)
        {
            if (strcmp(s,"TITAN-REPOSITORY/1")!=0) return TITAN_REPOSITORY_PARSE_BAD_HEADER;
            out->protocol_version=1u; continue;
        }
        if (!*s || *s=='#') continue;
        if (*s=='[')
        {
            size_t len=strlen(s); unsigned int i;
            if (len<11u || s[len-1]!=']' || strncmp(s,"[package ",9u)!=0) return TITAN_REPOSITORY_PARSE_BAD_SYNTAX;
            s[len-1]='\0'; s+=9;
            if (!safe_token(s)) return TITAN_REPOSITORY_PARSE_BAD_SYNTAX;
            if (out->package_count>=TITAN_REPOSITORY_MAX_PACKAGES) return TITAN_REPOSITORY_PARSE_TOO_MANY_PACKAGES;
            for(i=0u;i<out->package_count;i++) if(!strcmp(out->packages[i].package_id,s)) return TITAN_REPOSITORY_PARSE_DUPLICATE_PACKAGE;
            current=&out->packages[out->package_count++]; memset(current,0,sizeof(*current));
            if(!copy_text(current->package_id,sizeof(current->package_id),s)) return TITAN_REPOSITORY_PARSE_BAD_SYNTAX;
            continue;
        }
        eq=strchr(s,'='); if(!eq) return TITAN_REPOSITORY_PARSE_BAD_SYNTAX; *eq++='\0'; s=trim(s); eq=trim(eq);
        if (!current)
        {
            if(!strcmp(s,"id")){if(!safe_token(eq)||!copy_text(out->repository_id,sizeof(out->repository_id),eq))return TITAN_REPOSITORY_PARSE_BAD_SYNTAX;}
            else if(!strcmp(s,"name")){if(!safe_text(eq)||!copy_text(out->repository_name,sizeof(out->repository_name),eq))return TITAN_REPOSITORY_PARSE_BAD_SYNTAX;}
            else return TITAN_REPOSITORY_PARSE_BAD_SYNTAX;
        }
        else
        {
            if(!strcmp(s,"id")) { if(strcmp(current->package_id,eq)!=0) return TITAN_REPOSITORY_PARSE_BAD_SYNTAX; }
            else if(!set_field(current,s,eq)) return TITAN_REPOSITORY_PARSE_BAD_SYNTAX;
        }
    }
    if (!safe_token(out->repository_id) || !out->repository_name[0]) return TITAN_REPOSITORY_PARSE_BAD_SYNTAX;
    { unsigned int i; for(i=0u;i<out->package_count;i++) if(!package_complete(&out->packages[i])) return TITAN_REPOSITORY_PARSE_INVALID_PACKAGE; }
    return TITAN_REPOSITORY_PARSE_OK;
}
