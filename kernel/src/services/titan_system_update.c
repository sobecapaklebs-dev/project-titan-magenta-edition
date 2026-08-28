#include "titan_system_update.h"
#include "titan_storage_io.h"
#include "titan_storage_paths.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#define TITAN_PUP_MAGIC "TPUP"
#define TITAN_PUP_FORMAT 1u
#define TITAN_PUP_HEADER_SIZE 64u
#define TITAN_PUP_ENTRY_SIZE 144u
#define TITAN_PUP_MAX_FILES 16u
#define TITAN_PUP_CHUNK 4096u
#define TITAN_PUP_ROOT TITAN_STORAGE_DEVICE_ROOT "/update"
#define TITAN_PUP_PRIMARY_ROOT TITAN_STORAGE_DEVICE_ROOT "/update"
#define TITAN_PUP_PACKAGE_ROOT TITAN_STORAGE_DEVICE_ROOT "/packages"
#define TITAN_PUP_FALLBACK_ROOT "mass0:/TITAN/update"
#define TITAN_PUP_PACKAGE_FALLBACK_ROOT "mass0:/TITAN/packages"
#ifndef TITAN_SYSTEM_UPDATE_DEVICE_ROOT
#define TITAN_SYSTEM_UPDATE_DEVICE_ROOT "mass:"
#endif

typedef struct TitanPupEntry
{
    char target[128];
    unsigned int offset;
    unsigned int size;
    unsigned int checksum;
    unsigned int flags;
} TitanPupEntry;

typedef struct TitanSystemUpdateRuntime
{
    TitanSystemUpdateInfo updates[TITAN_SYSTEM_UPDATE_CAPACITY];
    unsigned int update_count;
    TitanSystemUpdateStatus status;
    TitanSystemUpdateInfo selected;
    TitanPupEntry entries[TITAN_PUP_MAX_FILES];
    unsigned int payload_offset;
    int input;
    int output;
    unsigned int current_bytes;
    unsigned int current_crc;
    unsigned int transferred;
    unsigned int total_payload;
    unsigned int verify_file_index;
    unsigned int verify_bytes;
    unsigned int verify_crc;
    unsigned int transfer_size;
    unsigned int transfer_offset;
    unsigned int staged_verify_index;
    unsigned int cancel_requested;
    unsigned int attempt_generation;
    unsigned char buffer[TITAN_PUP_CHUNK] __attribute__((aligned(64)));
    char stage_root[256];
    char stage_path[256];
} TitanSystemUpdateRuntime;

static TitanSystemUpdateRuntime g_update;

static unsigned int TitanSystemUpdate_U32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8u) |
        ((unsigned int)p[2] << 16u) | ((unsigned int)p[3] << 24u);
}

static unsigned int TitanSystemUpdate_Crc32(unsigned int crc, const void *data, unsigned int size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    unsigned int i;
    while (size-- > 0u)
    {
        crc ^= *bytes++;
        for (i = 0u; i < 8u; i++)
            crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static void TitanSystemUpdate_Copy(char *dst, unsigned int cap, const char *src)
{
    unsigned int i = 0u;
    if (dst == 0 || cap == 0u) return;
    while (src != 0 && src[i] != '\0' && i + 1u < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int TitanSystemUpdate_EndsPup(const char *name)
{
    unsigned int n;
    if (name == 0) return 0;
    n = (unsigned int)strlen(name);
    return n >= 5u && name[n-4u] == '.' &&
        tolower((unsigned char)name[n-3u]) == 'p' &&
        tolower((unsigned char)name[n-2u]) == 'u' &&
        tolower((unsigned char)name[n-1u]) == 'p';
}

static int TitanSystemUpdate_TargetSafe(const char *target)
{
    if (target == 0 || target[0] == '\0' || target[0] == '/' ||
        strstr(target, "..") != 0 || strchr(target, ':') != 0 || strchr(target, '\\') != 0)
        return 0;
    return strcmp(target, "kernel.elf") == 0 || strcmp(target, "kernel") == 0 ||
        strncmp(target, "TITAN/", 6u) == 0 || strncmp(target, "bootloader/", 11u) == 0;
}

static int TitanSystemUpdate_PathJoin(char *out, unsigned int cap, const char *a, const char *b)
{
    int n = snprintf(out, cap, "%s/%s", a, b);
    return n >= 0 && (unsigned int)n < cap;
}

static int TitanSystemUpdate_CreateParents(const char *path)
{
    char temp[256];
    unsigned int i;
    TitanSystemUpdate_Copy(temp, sizeof(temp), path);
    for (i = 6u; temp[i] != '\0'; i++)
    {
        if (temp[i] == '/')
        {
            temp[i] = '\0';
            if (!TitanStorageIo_PathInfo64(temp, 0, 0) && TitanStorageIo_CreateDirectory(temp) < 0)
                return 0;
            temp[i] = '/';
        }
    }
    return 1;
}

static int TitanSystemUpdate_Inspect(const char *path, TitanSystemUpdateInfo *info,
    TitanPupEntry *entries, unsigned int *payload_offset)
{
    unsigned char header[TITAN_PUP_HEADER_SIZE];
    unsigned char raw[TITAN_PUP_ENTRY_SIZE];
    unsigned int count, i, table_crc, crc = 0xFFFFFFFFu, total = 0u;
    unsigned int expected_offset = 0u;
    unsigned int package_size;
    TitanStorageIoSize actual_size;
    int fd;

    if (!TitanStorageIo_PathInfo64(path, 0, &actual_size) || actual_size > 0xFFFFFFFFu)
        return 0;
    fd = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (fd < 0) return 0;
    if (!TitanStorageIo_ReadExact(fd, header, sizeof(header)) ||
        memcmp(header, TITAN_PUP_MAGIC, 4u) != 0 || TitanSystemUpdate_U32(header+4) != TITAN_PUP_FORMAT)
    { TitanStorageIo_Close(fd); return 0; }
    count = TitanSystemUpdate_U32(header+12);
    if (count == 0u || count > TITAN_PUP_MAX_FILES) { TitanStorageIo_Close(fd); return 0; }
    if (TitanSystemUpdate_U32(header+8) <= TITAN_SYSTEM_UPDATE_CURRENT_BUILD)
    { TitanStorageIo_Close(fd); return -1; }
    *payload_offset = TitanSystemUpdate_U32(header+16);
    package_size = TitanSystemUpdate_U32(header+20);
    table_crc = TitanSystemUpdate_U32(header+24);
    if (*payload_offset != TITAN_PUP_HEADER_SIZE + count*TITAN_PUP_ENTRY_SIZE ||
        package_size != (unsigned int)actual_size || package_size < *payload_offset)
    { TitanStorageIo_Close(fd); return 0; }
    memset(entries, 0, sizeof(TitanPupEntry)*TITAN_PUP_MAX_FILES);
    for (i=0u;i<count;i++)
    {
        if (!TitanStorageIo_ReadExact(fd, raw, sizeof(raw))) { TitanStorageIo_Close(fd); return 0; }
        crc = TitanSystemUpdate_Crc32(crc, raw, sizeof(raw));
        memcpy(entries[i].target, raw, 127u); entries[i].target[127]='\0';
        entries[i].offset=TitanSystemUpdate_U32(raw+128);
        entries[i].size=TitanSystemUpdate_U32(raw+132);
        entries[i].checksum=TitanSystemUpdate_U32(raw+136);
        entries[i].flags=TitanSystemUpdate_U32(raw+140);
        if (!TitanSystemUpdate_TargetSafe(entries[i].target) ||
            entries[i].flags != 0u ||
            entries[i].offset != expected_offset ||
            entries[i].offset > package_size - *payload_offset ||
            entries[i].size > package_size - *payload_offset - entries[i].offset)
        { TitanStorageIo_Close(fd); return 0; }
        {
            unsigned int previous;
            for (previous = 0u; previous < i; previous++)
            {
                if (strcmp(entries[previous].target, entries[i].target) == 0)
                { TitanStorageIo_Close(fd); return 0; }
            }
        }
        if (0xFFFFFFFFu - total < entries[i].size)
        { TitanStorageIo_Close(fd); return 0; }
        total += entries[i].size;
        expected_offset += entries[i].size;
    }
    TitanStorageIo_Close(fd);
    if ((crc ^ 0xFFFFFFFFu) != table_crc ||
        total != package_size - *payload_offset) return 0;
    {
        /* `path` may point at info->path when Begin() re-validates the selected
         * package in place. Preserve it before clearing the output structure;
         * otherwise the memset erases the source string and the subsequent
         * open() sees an empty path. */
        char inspected_path[256];
        const char *slash;
        TitanSystemUpdate_Copy(inspected_path, sizeof(inspected_path), path);
        memset(info, 0, sizeof(*info));
        TitanSystemUpdate_Copy(info->path, sizeof(info->path), inspected_path);
        slash = strrchr(inspected_path, '/');
        TitanSystemUpdate_Copy(
            info->filename,
            sizeof(info->filename),
            slash != 0 ? slash + 1 : inspected_path
        );
    }
    memcpy(info->version, header+28, 31u); info->version[31]='\0';
    info->build_number=TitanSystemUpdate_U32(header+8);
    info->file_count=count;
    info->package_size=package_size;
    (void)total;
    return 1;
}

static unsigned int TitanSystemUpdate_ScanRoot(const char *root)
{
    DIR *dir = opendir(root); struct dirent *ent;
    if (dir == 0) return g_update.update_count;
    while (g_update.update_count < TITAN_SYSTEM_UPDATE_CAPACITY && (ent=readdir(dir)) != 0)
    {
        char path[256]; TitanPupEntry temp[TITAN_PUP_MAX_FILES]; unsigned int po; int result;
        if (!TitanSystemUpdate_EndsPup(ent->d_name) || !TitanSystemUpdate_PathJoin(path,sizeof(path),root,ent->d_name)) continue;
        result=TitanSystemUpdate_Inspect(path,&g_update.updates[g_update.update_count],temp,&po);
        if (result==1) g_update.update_count++;
    }
    closedir(dir); return g_update.update_count;
}

void TitanSystemUpdate_Initialize(void)
{
    memset(&g_update,0,sizeof(g_update)); g_update.input=-1; g_update.output=-1; g_update.attempt_generation=1u;
    g_update.status.phase=TITAN_SYSTEM_UPDATE_IDLE; g_update.status.result=TITAN_SYSTEM_UPDATE_OK;
}

unsigned int TitanSystemUpdate_Refresh(void)
{
    if (g_update.status.active) return g_update.update_count;
    memset(g_update.updates,0,sizeof(g_update.updates)); g_update.update_count=0u;
    TitanSystemUpdate_ScanRoot(TITAN_PUP_PRIMARY_ROOT);
    TitanSystemUpdate_ScanRoot(TITAN_PUP_PACKAGE_ROOT);
    TitanSystemUpdate_ScanRoot(TITAN_PUP_FALLBACK_ROOT);
    TitanSystemUpdate_ScanRoot(TITAN_PUP_PACKAGE_FALLBACK_ROOT);
    return g_update.update_count;
}
unsigned int TitanSystemUpdate_GetCount(void){return g_update.update_count;}
const TitanSystemUpdateInfo *TitanSystemUpdate_Get(unsigned int i){return i<g_update.update_count?&g_update.updates[i]:0;}

TitanSystemUpdateResult TitanSystemUpdate_Begin(unsigned int index)
{
    if (g_update.status.active) return TITAN_SYSTEM_UPDATE_STORAGE_ERROR;
    if (index>=g_update.update_count) return TITAN_SYSTEM_UPDATE_NO_PACKAGE;

    /* Do not touch FileXio here. The UI enables package-exclusive mode only
     * after Begin() succeeds, and the storage scheduler waits for its worker
     * to acknowledge the park before calling TitanSystemUpdate_Update().
     * All authoritative PUP I/O therefore starts in PREPARING, with a single
     * EE thread owning the storage client. */
    g_update.selected=g_update.updates[index];
    g_update.payload_offset=0u;
    g_update.total_payload=0u;
    g_update.transferred=0u;
    g_update.current_bytes=0u;
    g_update.current_crc=0xFFFFFFFFu;
    g_update.verify_file_index=0u;
    g_update.verify_bytes=0u;
    g_update.verify_crc=0xFFFFFFFFu;
    g_update.transfer_size=0u;
    g_update.transfer_offset=0u;
    g_update.staged_verify_index=0u;
    g_update.cancel_requested=0u;
    g_update.input=-1;
    g_update.output=-1;
    g_update.stage_root[0]='\0';
    g_update.stage_path[0]='\0';
    memset(g_update.entries,0,sizeof(g_update.entries));
    memset(&g_update.status,0,sizeof(g_update.status));
    g_update.status.active=1u;
    g_update.status.phase=TITAN_SYSTEM_UPDATE_PREPARING;
    g_update.status.result=TITAN_SYSTEM_UPDATE_OK;
    TitanSystemUpdate_Copy(g_update.status.message,sizeof(g_update.status.message),"Aguardando storage exclusivo");
    return TITAN_SYSTEM_UPDATE_OK;
}

static void TitanSystemUpdate_Fail(TitanSystemUpdateResult r,const char *msg)
{
    if(g_update.output>=0){TitanStorageIo_Close(g_update.output);g_update.output=-1;}
    if(g_update.input>=0){TitanStorageIo_Close(g_update.input);g_update.input=-1;}
    g_update.status.active=0u; g_update.status.phase=TITAN_SYSTEM_UPDATE_ERROR; g_update.status.result=r;
    TitanSystemUpdate_Copy(g_update.status.message,sizeof(g_update.status.message),msg);
}

static int TitanSystemUpdate_Publish(void);

static int TitanSystemUpdate_VerifyExtractedFile(const char *path, const TitanPupEntry *entry)
{
    TitanStorageIoSize size;
    int fd;
    unsigned int crc;
    unsigned int remaining;

    if (path == 0 || entry == 0 ||
        !TitanStorageIo_PathInfo64(path, 0, &size) ||
        size != (TitanStorageIoSize)entry->size)
    {
        return 0;
    }

    fd = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
    if (fd < 0)
    {
        return 0;
    }

    crc = 0xFFFFFFFFu;
    remaining = entry->size;
    while (remaining > 0u)
    {
        unsigned int chunk = remaining > TITAN_PUP_CHUNK ? TITAN_PUP_CHUNK : remaining;
        TitanStorageIoResult got = TitanStorageIo_ReadSome(fd, g_update.buffer, chunk);
        if (got <= 0)
        {
            TitanStorageIo_Close(fd);
            return 0;
        }
        crc = TitanSystemUpdate_Crc32(crc, g_update.buffer, (unsigned int)got);
        remaining -= (unsigned int)got;
    }

    TitanStorageIo_Close(fd);
    return (crc ^ 0xFFFFFFFFu) == entry->checksum;
}


static int TitanSystemUpdate_PrepareTransaction(void)
{
    int result;
    unsigned int i;

    /* This is the first authoritative read after the scheduler has parked the
     * background storage worker. Never trust the discovery-time inspection as
     * the install transaction's source of truth. */
    result = TitanSystemUpdate_Inspect(
        g_update.selected.path,
        &g_update.selected,
        g_update.entries,
        &g_update.payload_offset
    );
    if (result < 0)
    {
        TitanSystemUpdate_Fail(
            TITAN_SYSTEM_UPDATE_OLD_VERSION,
            "Downgrade/mesma versao bloqueado"
        );
        return 0;
    }
    if (result == 0)
    {
        TitanSystemUpdate_Fail(
            TITAN_SYSTEM_UPDATE_INVALID_PACKAGE,
            "PUP invalido ao revalidar em storage exclusivo"
        );
        return 0;
    }

    if (snprintf(
            g_update.stage_root,
            sizeof(g_update.stage_root),
            TITAN_PUP_ROOT "/.stage-%u-%08u",
            g_update.selected.build_number,
            g_update.attempt_generation++
        ) < 0)
    {
        TitanSystemUpdate_Fail(
            TITAN_SYSTEM_UPDATE_STORAGE_ERROR,
            "Falha ao preparar staging"
        );
        return 0;
    }

    if (!TitanStorageIo_PathInfo64(TITAN_PUP_ROOT, 0, 0) &&
        TitanStorageIo_CreateDirectory(TITAN_PUP_ROOT) < 0)
    {
        TitanSystemUpdate_Fail(
            TITAN_SYSTEM_UPDATE_STORAGE_ERROR,
            "Falha ao criar diretorio de update"
        );
        return 0;
    }
    if (TitanStorageIo_PathInfo64(g_update.stage_root, 0, 0) ||
        TitanStorageIo_CreateDirectory(g_update.stage_root) < 0)
    {
        TitanSystemUpdate_Fail(
            TITAN_SYSTEM_UPDATE_STORAGE_ERROR,
            "Falha ao criar staging da atualizacao"
        );
        return 0;
    }

    g_update.total_payload = 0u;
    for (i = 0u; i < g_update.selected.file_count; i++)
    {
        g_update.total_payload += g_update.entries[i].size;
    }

    g_update.status.file_count = g_update.selected.file_count;
    g_update.status.file_index = 0u;
    g_update.status.progress_percent = 0u;
    g_update.verify_file_index = 0u;
    g_update.verify_bytes = 0u;
    g_update.verify_crc = 0xFFFFFFFFu;
    g_update.current_bytes = 0u;
    g_update.current_crc = 0xFFFFFFFFu;
    g_update.transfer_size = 0u;
    g_update.transfer_offset = 0u;
    g_update.staged_verify_index = 0u;
    g_update.transferred = 0u;
    g_update.stage_path[0] = '\0';
    g_update.status.phase = TITAN_SYSTEM_UPDATE_VERIFYING;
    TitanSystemUpdate_Copy(
        g_update.status.message,
        sizeof(g_update.status.message),
        "Verificando integridade do PUP no USB"
    );
    return 1;
}

static int TitanSystemUpdate_DiscardExact(int descriptor, unsigned int size)
{
    while (size > 0u)
    {
        unsigned int chunk = size > TITAN_PUP_CHUNK ? TITAN_PUP_CHUNK : size;
        TitanStorageIoResult got = TitanStorageIo_ReadSome(
            descriptor,
            g_update.buffer,
            chunk
        );
        if (got <= 0 || (unsigned int)got > chunk)
        {
            return 0;
        }
        size -= (unsigned int)got;
    }
    return 1;
}

static int TitanSystemUpdate_OpenPayloadStream(void)
{
    if (g_update.input >= 0)
    {
        TitanStorageIo_Close(g_update.input);
        g_update.input = -1;
    }

    g_update.input = TitanStorageIo_Open(
        g_update.selected.path,
        TITAN_STORAGE_IO_READ
    );
    if (g_update.input < 0)
    {
        return 0;
    }

    /* Hardware fix: never call lseek() on the TPUP stream. The BuildFix23
     * telemetry proved that the first file already produced a different CRC
     * during a read-only pass, before staging existed. Header + file table are
     * at most 2368 bytes, so consume them sequentially and leave the descriptor
     * positioned exactly at payload byte zero. */
    if (!TitanSystemUpdate_DiscardExact(g_update.input, g_update.payload_offset))
    {
        TitanStorageIo_Close(g_update.input);
        g_update.input = -1;
        return 0;
    }
    return 1;
}

static int TitanSystemUpdate_PrepareStagePath(const TitanPupEntry *entry)
{
    if (entry == 0)
    {
        return 0;
    }
    if (!TitanSystemUpdate_PathJoin(
            g_update.stage_path,
            sizeof(g_update.stage_path),
            g_update.stage_root,
            entry->target
        ) ||
        !TitanSystemUpdate_CreateParents(g_update.stage_path))
    {
        return 0;
    }
    return 1;
}

static int TitanSystemUpdate_OpenStageOutput(const TitanPupEntry *entry)
{
    if (entry == 0 || !TitanSystemUpdate_PrepareStagePath(entry))
    {
        return 0;
    }

    g_update.output = TitanStorageIo_Open(
        g_update.stage_path,
        TITAN_STORAGE_IO_WRITE |
            TITAN_STORAGE_IO_CREATE |
            TITAN_STORAGE_IO_TRUNCATE
    );
    return g_update.output >= 0;
}

static void TitanSystemUpdate_ReportChecksumMismatch(
    const char *where,
    unsigned int file_index,
    unsigned int expected,
    unsigned int actual
)
{
    char message[112];
    (void)snprintf(
        message,
        sizeof(message),
        "%s arq %u exp %08X obt %08X",
        where,
        file_index + 1u,
        expected,
        actual
    );
    TitanSystemUpdate_Fail(
        TITAN_SYSTEM_UPDATE_CHECKSUM_ERROR,
        message
    );
}

static void TitanSystemUpdate_VerifyPayloadStep(unsigned int byte_budget)
{
    TitanPupEntry *entry;
    unsigned int remaining;
    unsigned int chunk;
    unsigned int actual_crc;
    TitanStorageIoResult got;

    if (g_update.input < 0)
    {
        if (!TitanSystemUpdate_OpenPayloadStream())
        {
            TitanSystemUpdate_Fail(
                TITAN_SYSTEM_UPDATE_STORAGE_ERROR,
                "Falha ao abrir stream sequencial do PUP"
            );
        }
        return;
    }

    if (g_update.verify_file_index >= g_update.selected.file_count)
    {
        TitanStorageIo_Close(g_update.input);
        g_update.input = -1;
        g_update.status.phase = TITAN_SYSTEM_UPDATE_EXTRACTING;
        g_update.status.progress_percent = 25u;
        g_update.status.file_index = 0u;
        g_update.current_bytes = 0u;
        g_update.current_crc = 0xFFFFFFFFu;
        g_update.transfer_size = 0u;
        g_update.transfer_offset = 0u;
        g_update.transferred = 0u;
        g_update.stage_path[0] = '\0';
        TitanSystemUpdate_Copy(
            g_update.status.message,
            sizeof(g_update.status.message),
            "PUP verificado em stream; iniciando extracao"
        );
        return;
    }

    entry = &g_update.entries[g_update.verify_file_index];
    remaining = entry->size - g_update.verify_bytes;
    if (remaining == 0u)
    {
        actual_crc = g_update.verify_crc ^ 0xFFFFFFFFu;
        if (actual_crc != entry->checksum)
        {
            TitanSystemUpdate_ReportChecksumMismatch(
                "CRC stream PUP invalido",
                g_update.verify_file_index,
                entry->checksum,
                actual_crc
            );
            return;
        }
        g_update.verify_file_index++;
        g_update.verify_bytes = 0u;
        g_update.verify_crc = 0xFFFFFFFFu;
        return;
    }

    chunk = remaining;
    if (chunk > byte_budget) chunk = byte_budget;
    if (chunk > TITAN_PUP_CHUNK) chunk = TITAN_PUP_CHUNK;
    if (chunk == 0u) return;

    got = TitanStorageIo_ReadSome(g_update.input, g_update.buffer, chunk);
    if (got <= 0 || (unsigned int)got > chunk)
    {
        TitanSystemUpdate_Fail(
            TITAN_SYSTEM_UPDATE_STORAGE_ERROR,
            "Falha ao ler stream sequencial do PUP"
        );
        return;
    }

    g_update.verify_crc = TitanSystemUpdate_Crc32(
        g_update.verify_crc,
        g_update.buffer,
        (unsigned int)got
    );
    g_update.verify_bytes += (unsigned int)got;
    {
        unsigned int verified = g_update.verify_bytes;
        unsigned int i;
        for (i = 0u; i < g_update.verify_file_index; i++)
            verified += g_update.entries[i].size;
        g_update.status.progress_percent = g_update.total_payload
            ? (verified * 25u) / g_update.total_payload
            : 0u;
    }
    TitanSystemUpdate_Copy(
        g_update.status.message,
        sizeof(g_update.status.message),
        "Verificando PUP sem seek"
    );
}

static void TitanSystemUpdate_ExtractStep(unsigned int byte_budget)
{
    TitanPupEntry *entry;
    unsigned int remaining;
    unsigned int chunk;
    unsigned int actual_crc;

    if (g_update.input < 0)
    {
        if (!TitanSystemUpdate_OpenPayloadStream())
        {
            TitanSystemUpdate_Fail(
                TITAN_SYSTEM_UPDATE_STORAGE_ERROR,
                "Falha ao reabrir stream sequencial do PUP"
            );
        }
        return;
    }

    if (g_update.status.file_index >= g_update.selected.file_count)
    {
        TitanStorageIo_Close(g_update.input);
        g_update.input = -1;
        g_update.staged_verify_index = 0u;
        g_update.status.phase = TITAN_SYSTEM_UPDATE_VERIFYING_STAGED;
        g_update.status.progress_percent = 90u;
        TitanSystemUpdate_Copy(
            g_update.status.message,
            sizeof(g_update.status.message),
            "Relendo staging antes de publicar"
        );
        return;
    }

    entry = &g_update.entries[g_update.status.file_index];

    if (g_update.output < 0)
    {
        if (!TitanSystemUpdate_OpenStageOutput(entry))
        {
            TitanSystemUpdate_Fail(
                TITAN_SYSTEM_UPDATE_STORAGE_ERROR,
                "Falha ao abrir staging da atualizacao"
            );
        }
        return;
    }

    /* One RPC direction per scheduler quantum, matching the Package Installer:
     * read from PUP on one turn, write the buffered bytes on the next. The
     * descriptors stay sequential; no seek is used anywhere in the payload. */
    if (g_update.transfer_offset < g_update.transfer_size)
    {
        TitanStorageIoResult wrote;
        unsigned int pending = g_update.transfer_size - g_update.transfer_offset;
        chunk = pending > byte_budget ? byte_budget : pending;
        if (chunk == 0u) return;
        wrote = TitanStorageIo_WriteSome(
            g_update.output,
            g_update.buffer + g_update.transfer_offset,
            chunk
        );
        if (wrote <= 0 || (unsigned int)wrote > chunk)
        {
            TitanSystemUpdate_Fail(
                TITAN_SYSTEM_UPDATE_STORAGE_ERROR,
                "Falha ao gravar staging sequencial"
            );
            return;
        }
        g_update.current_crc = TitanSystemUpdate_Crc32(
            g_update.current_crc,
            g_update.buffer + g_update.transfer_offset,
            (unsigned int)wrote
        );
        g_update.transfer_offset += (unsigned int)wrote;
        g_update.current_bytes += (unsigned int)wrote;
        g_update.transferred += (unsigned int)wrote;
        if (g_update.transfer_offset >= g_update.transfer_size)
        {
            g_update.transfer_size = 0u;
            g_update.transfer_offset = 0u;
        }
        g_update.status.progress_percent = g_update.total_payload
            ? 25u + (g_update.transferred * 65u / g_update.total_payload)
            : 25u;
        return;
    }

    remaining = entry->size - g_update.current_bytes;
    if (remaining == 0u)
    {
        actual_crc = g_update.current_crc ^ 0xFFFFFFFFu;
        if (TitanStorageIo_Close(g_update.output) < 0)
        {
            g_update.output = -1;
            TitanSystemUpdate_Fail(
                TITAN_SYSTEM_UPDATE_STORAGE_ERROR,
                "Falha ao fechar staging"
            );
            return;
        }
        g_update.output = -1;
        if (actual_crc != entry->checksum)
        {
            TitanSystemUpdate_ReportChecksumMismatch(
                "CRC extracao stream invalido",
                g_update.status.file_index,
                entry->checksum,
                actual_crc
            );
            return;
        }
        g_update.status.file_index++;
        g_update.current_bytes = 0u;
        g_update.current_crc = 0xFFFFFFFFu;
        g_update.stage_path[0] = '\0';
        return;
    }

    chunk = remaining;
    if (chunk > sizeof(g_update.buffer)) chunk = sizeof(g_update.buffer);
    if (chunk > byte_budget) chunk = byte_budget;
    if (chunk == 0u) return;

    {
        TitanStorageIoResult got = TitanStorageIo_ReadSome(
            g_update.input,
            g_update.buffer,
            chunk
        );
        if (got <= 0 || (unsigned int)got > chunk)
        {
            TitanSystemUpdate_Fail(
                TITAN_SYSTEM_UPDATE_STORAGE_ERROR,
                "Falha ao ler payload sequencial durante extracao"
            );
            return;
        }
        g_update.transfer_size = (unsigned int)got;
        g_update.transfer_offset = 0u;
    }
}

static void TitanSystemUpdate_VerifyStagedStep(void)
{
    TitanPupEntry *entry;

    if (g_update.staged_verify_index >= g_update.selected.file_count)
    {
        g_update.status.phase = TITAN_SYSTEM_UPDATE_PUBLISHING;
        TitanSystemUpdate_Copy(
            g_update.status.message,
            sizeof(g_update.status.message),
            "Aplicando arquivos da atualizacao"
        );
        if (!TitanSystemUpdate_Publish())
        {
            TitanSystemUpdate_Fail(
                TITAN_SYSTEM_UPDATE_PUBLISH_ERROR,
                "Falha ao publicar atualizacao"
            );
            return;
        }
        g_update.status.active = 0u;
        g_update.status.phase = TITAN_SYSTEM_UPDATE_COMPLETE;
        g_update.status.result = TITAN_SYSTEM_UPDATE_OK;
        g_update.status.progress_percent = 100u;
        TitanSystemUpdate_Copy(
            g_update.status.message,
            sizeof(g_update.status.message),
            "Atualizacao aplicada; reinicio automatico"
        );
        return;
    }

    entry = &g_update.entries[g_update.staged_verify_index];
    if (!TitanSystemUpdate_PrepareStagePath(entry) ||
        !TitanSystemUpdate_VerifyExtractedFile(g_update.stage_path, entry))
    {
        TitanSystemUpdate_Fail(
            TITAN_SYSTEM_UPDATE_CHECKSUM_ERROR,
            "CRC do staging divergiu apos releitura"
        );
        return;
    }
    g_update.staged_verify_index++;
    if (g_update.selected.file_count != 0u)
        g_update.status.progress_percent =
            90u + (g_update.staged_verify_index * 5u /
                   g_update.selected.file_count);
}


static int TitanSystemUpdate_Publish(void)
{
    unsigned int i;
    unsigned int published;
    unsigned char original_exists[TITAN_PUP_MAX_FILES];
    char stage[256];
    char target[256];
    char backup[256];

    memset(original_exists, 0, sizeof(original_exists));
    published = 0u;

    for (i = 0u; i < g_update.selected.file_count; i++)
    {
        if (!TitanSystemUpdate_PathJoin(
                stage, sizeof(stage), g_update.stage_root,
                g_update.entries[i].target) ||
            snprintf(
                target, sizeof(target),
                TITAN_SYSTEM_UPDATE_DEVICE_ROOT "/%s",
                g_update.entries[i].target) < 0 ||
            snprintf(
                backup, sizeof(backup),
                TITAN_PUP_ROOT "/backup-%u-%u",
                g_update.selected.build_number, i) < 0 ||
            !TitanSystemUpdate_CreateParents(target))
        {
            break;
        }

        original_exists[i] =
            TitanStorageIo_PathInfo64(target, 0, 0) ? 1u : 0u;

        if (!TitanStorageIo_AtomicReplace(stage, target, backup))
        {
            break;
        }
        published++;
    }

    if (published != g_update.selected.file_count)
    {
        /* Restore every target already swapped during this transaction.  A
         * PUP may contain kernel.elf plus several TITAN files; a failure half
         * way through must not leave a mixed-version installation. */
        while (published > 0u)
        {
            unsigned int rollback_index = published - 1u;
            (void)snprintf(
                target, sizeof(target),
                TITAN_SYSTEM_UPDATE_DEVICE_ROOT "/%s",
                g_update.entries[rollback_index].target
            );
            (void)snprintf(
                backup, sizeof(backup),
                TITAN_PUP_ROOT "/backup-%u-%u",
                g_update.selected.build_number, rollback_index
            );

            (void)TitanStorageIo_Remove(target);
            if (original_exists[rollback_index])
            {
                (void)TitanStorageIo_Rename(backup, target);
            }
            else
            {
                (void)TitanStorageIo_Remove(backup);
            }
            published--;
        }
        return 0;
    }

    /* Backups exist only as transaction scratch.  Successful updates do not
     * create a persistent downgrade path. */
    for (i = 0u; i < g_update.selected.file_count; i++)
    {
        if (snprintf(
                backup, sizeof(backup),
                TITAN_PUP_ROOT "/backup-%u-%u",
                g_update.selected.build_number, i) >= 0)
        {
            (void)TitanStorageIo_Remove(backup);
        }
    }
    return 1;
}

void TitanSystemUpdate_Update(unsigned int byte_budget)
{
    if (!g_update.status.active)
    {
        return;
    }
    if (g_update.cancel_requested)
    {
        g_update.cancel_requested = 0u;
        TitanSystemUpdate_Fail(
            TITAN_SYSTEM_UPDATE_CANCELLED,
            "Atualizacao cancelada"
        );
        return;
    }

    switch (g_update.status.phase)
    {
        case TITAN_SYSTEM_UPDATE_PREPARING:
            (void)TitanSystemUpdate_PrepareTransaction();
            return;

        case TITAN_SYSTEM_UPDATE_VERIFYING:
            TitanSystemUpdate_VerifyPayloadStep(byte_budget);
            return;

        case TITAN_SYSTEM_UPDATE_EXTRACTING:
            TitanSystemUpdate_ExtractStep(byte_budget);
            return;

        case TITAN_SYSTEM_UPDATE_VERIFYING_STAGED:
            TitanSystemUpdate_VerifyStagedStep();
            return;

        case TITAN_SYSTEM_UPDATE_PUBLISHING:
        case TITAN_SYSTEM_UPDATE_COMPLETE:
        case TITAN_SYSTEM_UPDATE_ERROR:
        case TITAN_SYSTEM_UPDATE_IDLE:
        default:
            return;
    }
}
void TitanSystemUpdate_Cancel(void){if(g_update.status.active)g_update.cancel_requested=1u;}
int TitanSystemUpdate_IsActive(void){return g_update.status.active!=0u;}
const TitanSystemUpdateStatus *TitanSystemUpdate_GetStatus(void){return &g_update.status;}
const char *TitanSystemUpdate_GetResultName(TitanSystemUpdateResult r)
{
    switch(r){case TITAN_SYSTEM_UPDATE_OK:return "OK";case TITAN_SYSTEM_UPDATE_NO_PACKAGE:return "Nenhum PUP encontrado";case TITAN_SYSTEM_UPDATE_INVALID_PACKAGE:return "PUP invalido";case TITAN_SYSTEM_UPDATE_OLD_VERSION:return "Downgrade/mesma versao bloqueado";case TITAN_SYSTEM_UPDATE_STORAGE_ERROR:return "Falha de armazenamento";case TITAN_SYSTEM_UPDATE_CHECKSUM_ERROR:return "Checksum invalido";case TITAN_SYSTEM_UPDATE_PUBLISH_ERROR:return "Falha ao aplicar atualizacao";case TITAN_SYSTEM_UPDATE_CANCELLED:return "Atualizacao cancelada";default:return "Erro de atualizacao";}
}
