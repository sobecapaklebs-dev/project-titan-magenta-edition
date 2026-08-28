#include "titan_app_sdk.h"

#include <stdint.h>
#include <stddef.h>

#define WLE_ENTRY_CAPACITY 96u
#define WLE_PATH_CAPACITY 256u
#define WLE_COPY_CHUNK (16u * 1024u)
#define WLE_VISIBLE_ROWS 10u
#define WLE_TEXT_VIEW_CAPACITY (8u * 1024u)
#define WLE_TEXT_EDIT_CAPACITY 255u
#define WLE_TEXT_VISIBLE_LINES 13u
#define WLE_RECURSIVE_DEPTH 8u

#define WLE_ACTION_NONE 0u
#define WLE_ACTION_COPY 1u
#define WLE_ACTION_MOVE 2u
#define WLE_ACTION_RENAME 3u
#define WLE_ACTION_NEW_FOLDER 4u
#define WLE_ACTION_DELETE 5u
#define WLE_ACTION_COUNT 5u

#define WLE_TEXT_NONE 0u
#define WLE_TEXT_RENAME 1u
#define WLE_TEXT_NEW_FOLDER 2u
#define WLE_TEXT_EDIT_FILE 3u
#define WLE_TEXT_PFS_MOUNT 4u

typedef struct WleRecursiveFrame
{
    char source[WLE_PATH_CAPACITY];
    char destination[WLE_PATH_CAPACITY];
    uint32_t next_index;
} WleRecursiveFrame;

typedef struct WleState
{
    const TitanAppApiV1 *api;
    TitanAppStorageEntry entries[WLE_ENTRY_CAPACITY];
    uint32_t entry_count;
    uint32_t selected;
    uint32_t scroll;
    uint32_t action_menu;
    uint32_t action_selected;
    uint8_t refresh_pending;
    uint8_t copy_marked;
    uint8_t copy_active;
    uint8_t copy_failed;
    uint8_t copy_move;
    uint8_t text_action;
    uint8_t viewer_active;
    uint8_t image_viewer_active;
    uint8_t recursive_active;
    uint8_t recursive_move;
    uint8_t recursive_delete_phase;
    uint8_t recursive_depth;
    uint8_t copy_source_directory;
    uint8_t ftp_enabled;
    uint8_t vmc_view_active;
    uint8_t vmc_export_active;
    uint8_t *copy_buffer;
    uint8_t *viewer_buffer;
    uint64_t copy_offset;
    uint64_t copy_total;
    uint32_t viewer_size;
    uint32_t viewer_line;
    uint32_t vmc_export_offset;
    uint32_t vmc_export_size;
    char path[WLE_PATH_CAPACITY];
    char vmc_path[WLE_PATH_CAPACITY];
    char vmc_export_source[WLE_PATH_CAPACITY];
    char vmc_export_destination[WLE_PATH_CAPACITY];
    char vmc_export_temporary[WLE_PATH_CAPACITY];
    char copy_source[WLE_PATH_CAPACITY];
    char copy_destination[WLE_PATH_CAPACITY];
    char copy_temporary[WLE_PATH_CAPACITY];
    char recursive_root_source[WLE_PATH_CAPACITY];
    char recursive_root_destination[WLE_PATH_CAPACITY];
    char text_target[WLE_PATH_CAPACITY];
    char viewer_path[WLE_PATH_CAPACITY];
    char text_value[WLE_PATH_CAPACITY];
    WleRecursiveFrame recursive[WLE_RECURSIVE_DEPTH];
    char status[96];
} WleState;

static WleState g_wle;

static uint32_t wl_strlen(const char *text)
{
    uint32_t n = 0u;
    while (text != 0 && text[n] != '\0') n++;
    return n;
}

static void wl_copy(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0u;
    if (dst == 0 || cap == 0u) return;
    while (src != 0 && src[i] != '\0' && i + 1u < cap)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int wl_equal(const char *a, const char *b)
{
    uint32_t i = 0u;
    if (a == 0 || b == 0) return 0;
    while (a[i] != '\0' && b[i] != '\0')
    {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int wl_suffix_elf(const char *text)
{
    uint32_t n = wl_strlen(text);
    if (n < 4u) return 0;
    return text[n-4u] == '.' &&
        (text[n-3u] == 'e' || text[n-3u] == 'E') &&
        (text[n-2u] == 'l' || text[n-2u] == 'L') &&
        (text[n-1u] == 'f' || text[n-1u] == 'F');
}

static char wl_ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int wl_suffix_text(const char *text)
{
    static const char *const suffixes[] = {".txt", ".cfg", ".ini", ".log", ".md", ".json", ".xml"};
    uint32_t n = wl_strlen(text);
    uint32_t sidx;
    for (sidx = 0u; sidx < (uint32_t)(sizeof(suffixes) / sizeof(suffixes[0])); sidx++)
    {
        const char *suffix = suffixes[sidx];
        uint32_t sn = wl_strlen(suffix);
        uint32_t i;
        if (n < sn) continue;
        for (i = 0u; i < sn; i++)
            if (wl_ascii_lower(text[n - sn + i]) != suffix[i]) break;
        if (i == sn) return 1;
    }
    return 0;
}

static int wl_suffix_image(const char *text)
{
    static const char *const suffixes[] = {".jpg", ".jpeg", ".png"};
    uint32_t n = wl_strlen(text);
    uint32_t sidx;
    for (sidx = 0u; sidx < (uint32_t)(sizeof(suffixes) / sizeof(suffixes[0])); sidx++)
    {
        const char *suffix = suffixes[sidx];
        uint32_t sn = wl_strlen(suffix);
        uint32_t i;
        if (n < sn) continue;
        for (i = 0u; i < sn; i++)
            if (wl_ascii_lower(text[n - sn + i]) != suffix[i]) break;
        if (i == sn) return 1;
    }
    return 0;
}

static int wl_text_payload_safe(const uint8_t *data, uint32_t size)
{
    uint32_t i;
    if (data == 0) return 0;
    for (i = 0u; i < size; i++)
    {
        uint8_t c = data[i];
        if (c == 0u) return 0;
        if (c < 0x20u && c != '\n' && c != '\r' && c != '\t') return 0;
    }
    return 1;
}

static int wl_open_text_viewer(const char *path, uint64_t size)
{
    int got;
    if (path == 0 || size >= WLE_TEXT_VIEW_CAPACITY)
    {
        wl_copy(g_wle.status, sizeof(g_wle.status), "Texto grande demais para o viewer (limite 8 KiB)");
        return 0;
    }
    if (g_wle.viewer_buffer == 0)
        g_wle.viewer_buffer = (uint8_t *)g_wle.api->alloc(WLE_TEXT_VIEW_CAPACITY);
    if (g_wle.viewer_buffer == 0)
    {
        wl_copy(g_wle.status, sizeof(g_wle.status), "Sem memoria para o viewer de texto");
        return 0;
    }
    got = g_wle.api->storage_read(path, 0u, g_wle.viewer_buffer, (uint32_t)size);
    if (got < 0 || (uint64_t)(uint32_t)got != size || !wl_text_payload_safe(g_wle.viewer_buffer, (uint32_t)got))
    {
        wl_copy(g_wle.status, sizeof(g_wle.status), "Arquivo nao e texto suportado ou mudou durante a leitura");
        return 0;
    }
    g_wle.viewer_buffer[(uint32_t)got] = '\0';
    g_wle.viewer_size = (uint32_t)got;
    g_wle.viewer_line = 0u;
    g_wle.viewer_active = 1u;
    wl_copy(g_wle.viewer_path, sizeof(g_wle.viewer_path), path);
    wl_copy(g_wle.status, sizeof(g_wle.status),
        g_wle.viewer_size <= WLE_TEXT_EDIT_CAPACITY ? "O voltar  QUADRADO editar" : "O voltar  Somente leitura (>255 bytes)");
    return 1;
}

static int wl_suffix_path(char *path, uint32_t cap, const char *suffix)
{
    uint32_t n = wl_strlen(path), i = 0u;
    while (suffix != 0 && suffix[i] != '\0')
    {
        if (n + 1u >= cap) return 0;
        path[n++] = suffix[i++];
    }
    path[n] = '\0';
    return 1;
}

static int wl_append(char *path, uint32_t cap, const char *name)
{
    uint32_t length = wl_strlen(path);
    uint32_t i = 0u;
    int need_slash;
    if (length == 0u || name == 0) return 0;
    need_slash = path[length-1u] != '/' && path[length-1u] != ':';
    if (need_slash)
    {
        if (length + 1u >= cap) return 0;
        path[length++] = '/';
    }
    while (name[i] != '\0')
    {
        if (length + 1u >= cap) return 0;
        path[length++] = name[i++];
    }
    path[length] = '\0';
    return 1;
}

static int wl_path_is_descendant(const char *candidate, const char *parent)
{
    uint32_t i = 0u;
    if (candidate == 0 || parent == 0) return 0;
    while (parent[i] != '\0' && candidate[i] == parent[i]) i++;
    if (parent[i] != '\0') return 0;
    return candidate[i] == '/' || candidate[i] == '\0';
}

static int wl_is_root(const char *path)
{
    uint32_t i;
    if (path == 0) return 0;
    for (i = 0u; path[i] != '\0'; i++)
    {
        if (path[i] == ':' && path[i+1u] == '/' && path[i+2u] == '\0') return 1;
    }
    return 0;
}

static void wl_parent(void)
{
    uint32_t length = wl_strlen(g_wle.path);
    if (length == 0u) return;
    if (wl_is_root(g_wle.path))
    {
        g_wle.path[0] = '\0';
        g_wle.refresh_pending = 1u;
        return;
    }
    while (length > 0u && g_wle.path[length-1u] == '/') length--;
    while (length > 0u && g_wle.path[length-1u] != '/' && g_wle.path[length-1u] != ':') length--;
    if (length == 0u) g_wle.path[0] = '\0';
    else
    {
        if (g_wle.path[length-1u] == ':') g_wle.path[length] = '\0';
        else g_wle.path[length] = '\0';
    }
    g_wle.refresh_pending = 1u;
}

static void wl_vmc_parent(void)
{
    uint32_t length = wl_strlen(g_wle.vmc_path);
    if (length == 0u) return;
    while (length > 0u && g_wle.vmc_path[length - 1u] != '/') length--;
    if (length == 0u) g_wle.vmc_path[0] = '\0';
    else g_wle.vmc_path[length - 1u] = '\0';
    g_wle.refresh_pending = 1u;
}

static int wl_vmc_append(char *path, uint32_t cap, const char *name)
{
    if (path[0] == '\0')
    {
        wl_copy(path, cap, name);
        return path[0] != '\0';
    }
    return wl_append(path, cap, name);
}

static void wl_set_device_entry(uint32_t index, const char *name)
{
    TitanAppStorageEntry *entry = &g_wle.entries[index];
    uint32_t i;
    for (i = 0u; i < sizeof(*entry); i++) ((uint8_t *)entry)[i] = 0u;
    wl_copy(entry->name, TITAN_APP_STORAGE_NAME_CAPACITY, name);
    entry->flags = TITAN_APP_STORAGE_ENTRY_DIRECTORY;
}

static void wl_refresh(void)
{
    int count;
    g_wle.selected = 0u;
    g_wle.scroll = 0u;
    g_wle.action_menu = 0u;
    if (g_wle.vmc_view_active)
    {
        uint32_t i;
        for (i = 0u; i < WLE_ENTRY_CAPACITY; i++)
        {
            TitanAppVmcEntry ve;
            int vr = g_wle.api->vmc_list_active_path(g_wle.vmc_path, i, &ve);
            if (vr <= 0) break;
            wl_set_device_entry(i, ve.name);
            g_wle.entries[i].size = ve.size;
            g_wle.entries[i].flags = TITAN_APP_STORAGE_ENTRY_READ_ONLY;
            if ((ve.flags & TITAN_APP_VMC_ENTRY_DIRECTORY) != 0u)
                g_wle.entries[i].flags |= TITAN_APP_STORAGE_ENTRY_DIRECTORY;
        }
        g_wle.entry_count = i;
        wl_copy(g_wle.status, sizeof(g_wle.status),
            i == 0u ? "VMC vazia/indisponivel  O subir  R2 sair" : "VMC ativa (somente leitura)  X abrir  O subir  R2 sair");
        g_wle.refresh_pending = 0u;
        return;
    }
    if (g_wle.path[0] == '\0')
    {
        TitanAppStorageVolume volumes[WLE_ENTRY_CAPACITY];
        uint32_t i;
        count = g_wle.api->storage_list_volumes(0u, volumes, WLE_ENTRY_CAPACITY);
        if (count < 0) count = 0;
        for (i = 0u; i < (uint32_t)count; i++)
        {
            wl_set_device_entry(i, volumes[i].root);
            if ((volumes[i].flags & TITAN_APP_STORAGE_VOLUME_READ_ONLY) != 0u)
                g_wle.entries[i].flags |= TITAN_APP_STORAGE_ENTRY_READ_ONLY;
        }
        g_wle.entry_count = (uint32_t)count;
        wl_copy(g_wle.status, sizeof(g_wle.status),
            count == 0 ? "Nenhum volume online" : "Selecione um dispositivo");
        g_wle.refresh_pending = 0u;
        return;
    }
    count = g_wle.api->storage_list(g_wle.path, 0u, g_wle.entries, WLE_ENTRY_CAPACITY);
    if (count < 0)
    {
        g_wle.entry_count = 0u;
        wl_copy(g_wle.status, sizeof(g_wle.status), "Nao foi possivel abrir este dispositivo/pasta");
    }
    else
    {
        g_wle.entry_count = (uint32_t)count;
        wl_copy(g_wle.status, sizeof(g_wle.status), count == 0 ? "Pasta vazia" : "X abrir  SELECT acoes  O voltar");
    }
    g_wle.refresh_pending = 0u;
}

static void wl_selected_path(char *out, uint32_t cap)
{
    if (g_wle.entry_count == 0u || g_wle.selected >= g_wle.entry_count)
    {
        if (cap != 0u) out[0] = '\0';
        return;
    }
    if (g_wle.vmc_view_active)
    {
        if (cap != 0u) out[0] = '\0';
        return;
    }
    if (g_wle.path[0] == '\0')
    {
        wl_copy(out, cap, g_wle.entries[g_wle.selected].name);
        return;
    }
    wl_copy(out, cap, g_wle.path);
    if (!wl_append(out, cap, g_wle.entries[g_wle.selected].name)) out[0] = '\0';
}

static void wl_open_selected(void)
{
    TitanAppStorageEntry *entry;
    char target[WLE_PATH_CAPACITY];
    int result;
    if (g_wle.entry_count == 0u || g_wle.selected >= g_wle.entry_count) return;
    entry = &g_wle.entries[g_wle.selected];
    if (g_wle.vmc_view_active)
    {
        if ((entry->flags & TITAN_APP_STORAGE_ENTRY_DIRECTORY) != 0u)
        {
            char next[WLE_PATH_CAPACITY];
            wl_copy(next, sizeof(next), g_wle.vmc_path);
            if (!wl_vmc_append(next, sizeof(next), entry->name))
            {
                wl_copy(g_wle.status, sizeof(g_wle.status), "Caminho VMC excede limite Titan");
                return;
            }
            wl_copy(g_wle.vmc_path, sizeof(g_wle.vmc_path), next);
            g_wle.refresh_pending = 1u;
            return;
        }
        wl_copy(g_wle.status, sizeof(g_wle.status), "Arquivo VMC read-only; exportacao entra no proximo bloco");
        return;
    }
    wl_selected_path(target, sizeof(target));
    if (target[0] == '\0') return;
    if ((entry->flags & TITAN_APP_STORAGE_ENTRY_DIRECTORY) != 0u)
    {
        wl_copy(g_wle.path, sizeof(g_wle.path), target);
        g_wle.refresh_pending = 1u;
        return;
    }
    if (wl_suffix_elf(entry->name))
    {
        result = g_wle.api->launch_elf(target);
        if (result < 0)
        {
            wl_copy(g_wle.status, sizeof(g_wle.status), "Titan recusou ou nao conseguiu iniciar o ELF");
            (void)g_wle.api->notify(TITAN_APP_NOTIFICATION_ERROR, "wLaunchELF", g_wle.status);
        }
        return;
    }
    if (wl_suffix_image(entry->name))
    {
        if (!g_wle.api->media_present_image_file(target))
        {
            wl_copy(g_wle.status, sizeof(g_wle.status), "Imagem recusada, grande demais ou invalida");
            return;
        }
        g_wle.image_viewer_active = 1u;
        wl_copy(g_wle.viewer_path, sizeof(g_wle.viewer_path), target);
        wl_copy(g_wle.status, sizeof(g_wle.status), "O voltar  Imagem inteira preservada (fit)");
        return;
    }
    if (wl_suffix_text(entry->name))
    {
        (void)wl_open_text_viewer(target, entry->size);
        return;
    }
    wl_copy(g_wle.status, sizeof(g_wle.status), "Formato sem viewer Titan nativo");
}

static void wl_mark_transfer(int move)
{
    TitanAppStorageEntry *entry;
    if (g_wle.path[0] == '\0' || g_wle.entry_count == 0u || g_wle.selected >= g_wle.entry_count) return;
    entry = &g_wle.entries[g_wle.selected];
    g_wle.copy_source_directory =
        (entry->flags & TITAN_APP_STORAGE_ENTRY_DIRECTORY) != 0u ? 1u : 0u;
    wl_selected_path(g_wle.copy_source, sizeof(g_wle.copy_source));
    g_wle.copy_total = entry->size;
    g_wle.copy_marked = 1u;
    g_wle.copy_move = move ? 1u : 0u;
    wl_copy(g_wle.status, sizeof(g_wle.status),
        move ? "Arquivo marcado para mover. TRIANGULO no destino"
             : "Arquivo marcado para copiar. TRIANGULO no destino");
}

static void wl_mark_copy(void) { wl_mark_transfer(0); }
static void wl_mark_move(void) { wl_mark_transfer(1); }

static void wl_recursive_abort(const char *message)
{
    g_wle.recursive_active = 0u;
    g_wle.copy_active = 0u;
    g_wle.copy_marked = 0u;
    g_wle.recursive_depth = 0u;
    wl_copy(g_wle.status, sizeof(g_wle.status), message);
}

static int wl_recursive_push(const char *source, const char *destination)
{
    WleRecursiveFrame *frame;
    if (g_wle.recursive_depth >= WLE_RECURSIVE_DEPTH) return 0;
    frame = &g_wle.recursive[g_wle.recursive_depth++];
    wl_copy(frame->source, sizeof(frame->source), source);
    wl_copy(frame->destination, sizeof(frame->destination), destination);
    frame->next_index = 0u;
    return frame->source[0] != '\0' && frame->destination[0] != '\0';
}

static int wl_recursive_start_file(const char *source, const char *destination, uint64_t total)
{
    wl_copy(g_wle.copy_source, sizeof(g_wle.copy_source), source);
    wl_copy(g_wle.copy_destination, sizeof(g_wle.copy_destination), destination);
    wl_copy(g_wle.copy_temporary, sizeof(g_wle.copy_temporary), destination);
    if (!wl_suffix_path(g_wle.copy_temporary, sizeof(g_wle.copy_temporary), ".titantmp")) return 0;
    (void)g_wle.api->storage_remove(g_wle.copy_temporary);
    if (g_wle.copy_buffer == 0)
        g_wle.copy_buffer = (uint8_t *)g_wle.api->alloc(WLE_COPY_CHUNK);
    if (g_wle.copy_buffer == 0) return 0;
    g_wle.copy_total = total;
    g_wle.copy_offset = 0u;
    g_wle.copy_failed = 0u;
    g_wle.copy_active = 1u;
    return 1;
}

static void wl_recursive_begin_delete(void)
{
    g_wle.recursive_depth = 0u;
    g_wle.recursive_delete_phase = 1u;
    if (!wl_recursive_push(g_wle.recursive_root_source, g_wle.recursive_root_destination))
        wl_recursive_abort("Falha ao iniciar limpeza da origem");
}

static void wl_recursive_step(void)
{
    WleRecursiveFrame *frame;
    TitanAppStorageEntry entry;
    char source[WLE_PATH_CAPACITY];
    char destination[WLE_PATH_CAPACITY];
    int count;
    if (!g_wle.recursive_active || g_wle.copy_active || g_wle.recursive_depth == 0u) return;
    frame = &g_wle.recursive[g_wle.recursive_depth - 1u];
    count = g_wle.api->storage_list(frame->source, frame->next_index, &entry, 1u);
    if (count < 0)
    {
        wl_recursive_abort(g_wle.recursive_delete_phase ?
            "Destino completo; falha ao listar origem para remover" :
            "Falha ao listar pasta; origem preservada");
        return;
    }
    if (count == 0)
    {
        char completed_source[WLE_PATH_CAPACITY];
        wl_copy(completed_source, sizeof(completed_source), frame->source);
        g_wle.recursive_depth--;
        if (g_wle.recursive_delete_phase && !g_wle.api->storage_remove(completed_source))
        {
            wl_recursive_abort("Destino completo; uma pasta de origem nao pode ser removida");
            return;
        }
        if (g_wle.recursive_depth == 0u)
        {
            if (g_wle.recursive_move && !g_wle.recursive_delete_phase)
            {
                wl_recursive_begin_delete();
                wl_copy(g_wle.status, sizeof(g_wle.status), "Copia concluida; removendo origem...");
                return;
            }
            g_wle.recursive_active = 0u;
            g_wle.copy_marked = 0u;
            wl_copy(g_wle.status, sizeof(g_wle.status),
                g_wle.recursive_move ? "Movimento recursivo concluido" : "Copia recursiva concluida");
            (void)g_wle.api->notify(TITAN_APP_NOTIFICATION_SUCCESS, "wLaunchELF", g_wle.status);
            g_wle.refresh_pending = 1u;
        }
        return;
    }
    frame->next_index++;
    if (wl_equal(entry.name, ".") || wl_equal(entry.name, "..")) return;
    wl_copy(source, sizeof(source), frame->source);
    wl_copy(destination, sizeof(destination), frame->destination);
    if (!wl_append(source, sizeof(source), entry.name) ||
        !wl_append(destination, sizeof(destination), entry.name))
    {
        wl_recursive_abort("Caminho excede limite durante operacao recursiva");
        return;
    }
    if ((entry.flags & TITAN_APP_STORAGE_ENTRY_DIRECTORY) != 0u)
    {
        if (g_wle.recursive_delete_phase)
        {
            if (!wl_recursive_push(source, destination))
                wl_recursive_abort("Profundidade de pasta excede limite seguro (8)");
            return;
        }
        {
            TitanAppStorageEntry existing;
            if (g_wle.api->storage_stat(destination, &existing))
            {
                wl_recursive_abort("Destino de pasta ja existe; merge recursivo recusado");
                return;
            }
        }
        if (!g_wle.api->storage_mkdir(destination) || !wl_recursive_push(source, destination))
        {
            wl_recursive_abort("Falha ao criar pasta ou profundidade excede 8");
            return;
        }
        return;
    }
    if (g_wle.recursive_delete_phase)
    {
        if (!g_wle.api->storage_remove(source))
            wl_recursive_abort("Destino completo; um arquivo de origem nao pode ser removido");
        return;
    }
    if (!wl_recursive_start_file(source, destination, entry.size))
        wl_recursive_abort("Falha ao iniciar arquivo da copia recursiva");
}

static void wl_begin_paste(void)
{
    const char *name;
    uint32_t length;
    if (!g_wle.copy_marked || g_wle.path[0] == '\0' || g_wle.copy_active) return;
    length = wl_strlen(g_wle.copy_source);
    name = g_wle.copy_source;
    while (length > 0u)
    {
        if (g_wle.copy_source[length-1u] == '/' || g_wle.copy_source[length-1u] == ':')
        {
            name = &g_wle.copy_source[length];
            break;
        }
        length--;
    }
    wl_copy(g_wle.copy_destination, sizeof(g_wle.copy_destination), g_wle.path);
    if (!wl_append(g_wle.copy_destination, sizeof(g_wle.copy_destination), name) ||
        wl_equal(g_wle.copy_source, g_wle.copy_destination))
    {
        wl_copy(g_wle.status, sizeof(g_wle.status), "Destino de copia invalido");
        return;
    }
    if (g_wle.copy_source_directory)
    {
        TitanAppStorageEntry existing;
        if (wl_path_is_descendant(g_wle.copy_destination, g_wle.copy_source))
        {
            wl_copy(g_wle.status, sizeof(g_wle.status), "Destino nao pode ficar dentro da pasta de origem");
            return;
        }
        if (g_wle.api->storage_stat(g_wle.copy_destination, &existing))
        {
            wl_copy(g_wle.status, sizeof(g_wle.status), "Destino de pasta ja existe; merge recusado");
            return;
        }
        if (!g_wle.api->storage_mkdir(g_wle.copy_destination))
        {
            wl_copy(g_wle.status, sizeof(g_wle.status), "Falha ao criar pasta de destino");
            return;
        }
        wl_copy(g_wle.recursive_root_source, sizeof(g_wle.recursive_root_source), g_wle.copy_source);
        wl_copy(g_wle.recursive_root_destination, sizeof(g_wle.recursive_root_destination), g_wle.copy_destination);
        g_wle.recursive_active = 1u;
        g_wle.recursive_move = g_wle.copy_move;
        g_wle.recursive_delete_phase = 0u;
        g_wle.recursive_depth = 0u;
        if (!wl_recursive_push(g_wle.recursive_root_source, g_wle.recursive_root_destination))
        {
            wl_recursive_abort("Falha ao iniciar operacao recursiva");
            return;
        }
        wl_copy(g_wle.status, sizeof(g_wle.status),
            g_wle.copy_move ? "Copiando arvore antes de remover origem..." : "Copiando pasta...");
        return;
    }
    wl_copy(g_wle.copy_temporary, sizeof(g_wle.copy_temporary), g_wle.copy_destination);
    if (!wl_suffix_path(g_wle.copy_temporary, sizeof(g_wle.copy_temporary), ".titantmp"))
    {
        wl_copy(g_wle.status, sizeof(g_wle.status), "Caminho temporario invalido");
        return;
    }
    (void)g_wle.api->storage_remove(g_wle.copy_temporary);
    if (g_wle.copy_buffer == 0)
        g_wle.copy_buffer = (uint8_t *)g_wle.api->alloc(WLE_COPY_CHUNK);
    if (g_wle.copy_buffer == 0)
    {
        wl_copy(g_wle.status, sizeof(g_wle.status), "Sem memoria para o buffer de copia");
        return;
    }
    g_wle.copy_offset = 0u;
    g_wle.copy_failed = 0u;
    g_wle.copy_active = 1u;
    wl_copy(g_wle.status, sizeof(g_wle.status), "Copiando...");
}

static void wl_vmc_export_abort(const char *message)
{
    if (g_wle.vmc_export_temporary[0] != '\0')
        (void)g_wle.api->storage_remove(g_wle.vmc_export_temporary);
    g_wle.vmc_export_active = 0u;
    g_wle.vmc_export_offset = 0u;
    g_wle.vmc_export_size = 0u;
    wl_copy(g_wle.status, sizeof(g_wle.status), message);
}

static void wl_begin_vmc_export(void)
{
    TitanAppStorageEntry *entry;
    char source[WLE_PATH_CAPACITY];
    char destination[WLE_PATH_CAPACITY];
    if (!g_wle.vmc_view_active || g_wle.path[0] == '\0' ||
        g_wle.entry_count == 0u || g_wle.selected >= g_wle.entry_count)
    {
        wl_copy(g_wle.status, sizeof(g_wle.status),
            "Abra uma pasta de destino antes de entrar na VMC");
        return;
    }
    entry = &g_wle.entries[g_wle.selected];
    if ((entry->flags & TITAN_APP_STORAGE_ENTRY_DIRECTORY) != 0u)
    {
        wl_copy(g_wle.status, sizeof(g_wle.status), "Entre na pasta VMC com X; exporte arquivos com QUADRADO");
        return;
    }
    wl_copy(source, sizeof(source), g_wle.vmc_path);
    if (!wl_vmc_append(source, sizeof(source), entry->name))
    {
        wl_copy(g_wle.status, sizeof(g_wle.status), "Caminho VMC invalido para exportacao");
        return;
    }
    wl_copy(destination, sizeof(destination), g_wle.path);
    if (!wl_append(destination, sizeof(destination), entry->name))
    {
        wl_copy(g_wle.status, sizeof(g_wle.status), "Caminho de destino excede limite Titan");
        return;
    }
    wl_copy(g_wle.vmc_export_source, sizeof(g_wle.vmc_export_source), source);
    wl_copy(g_wle.vmc_export_destination, sizeof(g_wle.vmc_export_destination), destination);
    wl_copy(g_wle.vmc_export_temporary, sizeof(g_wle.vmc_export_temporary), destination);
    if (!wl_suffix_path(g_wle.vmc_export_temporary, sizeof(g_wle.vmc_export_temporary), ".titantmp"))
    {
        wl_copy(g_wle.status, sizeof(g_wle.status), "Caminho temporario VMC invalido");
        return;
    }
    (void)g_wle.api->storage_remove(g_wle.vmc_export_temporary);
    if (g_wle.copy_buffer == 0)
        g_wle.copy_buffer = (uint8_t *)g_wle.api->alloc(WLE_COPY_CHUNK);
    if (g_wle.copy_buffer == 0)
    {
        wl_copy(g_wle.status, sizeof(g_wle.status), "Sem memoria para exportar VMC");
        return;
    }
    g_wle.vmc_export_offset = 0u;
    g_wle.vmc_export_size = (uint32_t)entry->size;
    g_wle.vmc_export_active = 1u;
    wl_copy(g_wle.status, sizeof(g_wle.status), "Exportando arquivo VMC...");
}

static void wl_vmc_export_step(void)
{
    uint32_t remaining;
    uint32_t wanted;
    uint32_t flags;
    int read_count;
    int write_count;
    if (!g_wle.vmc_export_active || g_wle.copy_buffer == 0) return;
    if (g_wle.vmc_export_offset >= g_wle.vmc_export_size)
    {
        if (!g_wle.api->storage_commit_replace(
                g_wle.vmc_export_temporary, g_wle.vmc_export_destination))
        {
            wl_vmc_export_abort("Falha ao publicar exportacao VMC; destino preservado");
            return;
        }
        g_wle.vmc_export_active = 0u;
        wl_copy(g_wle.status, sizeof(g_wle.status), "Exportacao VMC concluida");
        (void)g_wle.api->notify(TITAN_APP_NOTIFICATION_SUCCESS, "wLaunchELF", "Arquivo VMC exportado");
        return;
    }
    remaining = g_wle.vmc_export_size - g_wle.vmc_export_offset;
    wanted = remaining < WLE_COPY_CHUNK ? remaining : WLE_COPY_CHUNK;
    read_count = g_wle.api->vmc_read_active_file(
        g_wle.vmc_export_source, g_wle.vmc_export_offset,
        g_wle.copy_buffer, wanted);
    if (read_count <= 0 || (uint32_t)read_count > wanted)
    {
        wl_vmc_export_abort("Falha ao ler arquivo VMC; destino preservado");
        return;
    }
    flags = TITAN_APP_STORAGE_WRITE_CREATE;
    if (g_wle.vmc_export_offset == 0u) flags |= TITAN_APP_STORAGE_WRITE_TRUNCATE;
    write_count = g_wle.api->storage_write(
        g_wle.vmc_export_temporary, g_wle.vmc_export_offset,
        g_wle.copy_buffer, (uint32_t)read_count, flags);
    if (write_count != read_count)
    {
        wl_vmc_export_abort("Falha ao gravar temporario VMC; destino preservado");
        return;
    }
    g_wle.vmc_export_offset += (uint32_t)read_count;
}

static void wl_copy_step(void)
{
    int read_count;
    int write_count;
    uint32_t flags;
    if (!g_wle.copy_active || g_wle.copy_buffer == 0) return;
    read_count = g_wle.api->storage_read(
        g_wle.copy_source, g_wle.copy_offset, g_wle.copy_buffer, WLE_COPY_CHUNK);
    if (read_count < 0)
    {
        g_wle.copy_active = 0u;
        g_wle.copy_failed = 1u;
        (void)g_wle.api->storage_remove(g_wle.copy_temporary);
        if (g_wle.recursive_active)
        {
            wl_recursive_abort("Falha ao ler arvore; origem preservada, destino pode estar parcial");
            return;
        }
        wl_copy(g_wle.status, sizeof(g_wle.status), "Falha ao ler origem durante a copia");
        return;
    }
    if (read_count == 0)
    {
        if (!g_wle.api->storage_commit_replace(g_wle.copy_temporary, g_wle.copy_destination))
        {
            g_wle.copy_active = 0u;
            g_wle.copy_failed = 1u;
            (void)g_wle.api->storage_remove(g_wle.copy_temporary);
            if (g_wle.recursive_active)
            {
                wl_recursive_abort("Falha ao publicar arvore; origem preservada, destino pode estar parcial");
                return;
            }
            wl_copy(g_wle.status, sizeof(g_wle.status), "Falha ao publicar destino; origem preservada");
            return;
        }
        if (g_wle.recursive_active)
        {
            g_wle.copy_active = 0u;
            wl_copy(g_wle.status, sizeof(g_wle.status), "Copiando pasta...");
            return;
        }
        if (g_wle.copy_move && !g_wle.api->storage_remove(g_wle.copy_source))
        {
            g_wle.copy_active = 0u;
            g_wle.copy_marked = 0u;
            wl_copy(g_wle.status, sizeof(g_wle.status), "Destino publicado; origem nao pode ser removida");
            g_wle.refresh_pending = 1u;
            return;
        }
        g_wle.copy_active = 0u;
        g_wle.copy_marked = 0u;
        wl_copy(g_wle.status, sizeof(g_wle.status), g_wle.copy_move ? "Movimento concluido" : "Copia concluida");
        (void)g_wle.api->notify(TITAN_APP_NOTIFICATION_SUCCESS, "wLaunchELF",
            g_wle.copy_move ? "Movimento concluido" : "Copia concluida");
        g_wle.refresh_pending = 1u;
        return;
    }
    flags = TITAN_APP_STORAGE_WRITE_CREATE;
    if (g_wle.copy_offset == 0u) flags |= TITAN_APP_STORAGE_WRITE_TRUNCATE;
    write_count = g_wle.api->storage_write(
        g_wle.copy_temporary, g_wle.copy_offset, g_wle.copy_buffer,
        (uint32_t)read_count, flags);
    if (write_count != read_count)
    {
        g_wle.copy_active = 0u;
        g_wle.copy_failed = 1u;
        (void)g_wle.api->storage_remove(g_wle.copy_temporary);
        if (g_wle.recursive_active)
        {
            wl_recursive_abort("Falha ao gravar arvore; origem preservada, destino pode estar parcial");
            return;
        }
        wl_copy(g_wle.status, sizeof(g_wle.status), "Falha ao gravar temporario; destino preservado");
        return;
    }
    g_wle.copy_offset += (uint32_t)read_count;
}

static void wl_delete_selected(void)
{
    char target[WLE_PATH_CAPACITY];
    if (g_wle.path[0] == '\0' || g_wle.entry_count == 0u || g_wle.selected >= g_wle.entry_count) return;
    wl_selected_path(target, sizeof(target));
    if (target[0] == '\0') return;
    if (!g_wle.api->storage_remove(target))
    {
        wl_copy(g_wle.status, sizeof(g_wle.status), "Falha ao excluir (pastas devem estar vazias)");
        return;
    }
    wl_copy(g_wle.status, sizeof(g_wle.status), "Item excluido");
    (void)g_wle.api->notify(TITAN_APP_NOTIFICATION_SUCCESS, "wLaunchELF", "Item excluido");
    g_wle.refresh_pending = 1u;
}

static void wl_begin_rename(void)
{
    if (g_wle.path[0] == '\0' || g_wle.entry_count == 0u || g_wle.selected >= g_wle.entry_count) return;
    wl_selected_path(g_wle.text_target, sizeof(g_wle.text_target));
    if (g_wle.text_target[0] == '\0') return;
    if (g_wle.api->text_input_open("Renomear", g_wle.entries[g_wle.selected].name, 96u))
        g_wle.text_action = WLE_TEXT_RENAME;
}

static void wl_begin_new_folder(void)
{
    if (g_wle.path[0] == '\0') return;
    if (g_wle.api->text_input_open("Nova pasta", "", 96u))
        g_wle.text_action = WLE_TEXT_NEW_FOLDER;
}

static void wl_begin_text_edit(void)
{
    if (!g_wle.viewer_active || g_wle.viewer_buffer == 0) return;
    if (g_wle.viewer_size > WLE_TEXT_EDIT_CAPACITY)
    {
        wl_copy(g_wle.status, sizeof(g_wle.status), "Edicao limitada a 255 bytes; viewer continua seguro");
        return;
    }
    if (g_wle.api->text_input_open("Editar texto", (const char *)g_wle.viewer_buffer, WLE_TEXT_EDIT_CAPACITY))
        g_wle.text_action = WLE_TEXT_EDIT_FILE;
}

static int wl_publish_text_edit(const char *text)
{
    char temporary[WLE_PATH_CAPACITY];
    uint32_t size = wl_strlen(text);
    int written;
    if (size > WLE_TEXT_EDIT_CAPACITY || g_wle.viewer_path[0] == '\0') return 0;
    wl_copy(temporary, sizeof(temporary), g_wle.viewer_path);
    if (!wl_suffix_path(temporary, sizeof(temporary), ".titantmp")) return 0;
    (void)g_wle.api->storage_remove(temporary);
    written = g_wle.api->storage_write(temporary, 0u, text, size,
        TITAN_APP_STORAGE_WRITE_CREATE | TITAN_APP_STORAGE_WRITE_TRUNCATE);
    if (written != (int)size)
    {
        (void)g_wle.api->storage_remove(temporary);
        return 0;
    }
    if (!g_wle.api->storage_commit_replace(temporary, g_wle.viewer_path))
    {
        (void)g_wle.api->storage_remove(temporary);
        return 0;
    }
    if (g_wle.viewer_buffer != 0)
    {
        wl_copy((char *)g_wle.viewer_buffer, WLE_TEXT_VIEW_CAPACITY, text);
        g_wle.viewer_size = size;
        g_wle.viewer_line = 0u;
    }
    return 1;
}

static int wl_name_safe(const char *name)
{
    uint32_t i;
    if (name == 0 || name[0] == '\0' || wl_equal(name, ".") || wl_equal(name, "..")) return 0;
    for (i = 0u; name[i] != '\0'; i++)
        if (name[i] == '/' || name[i] == ':' || name[i] == '\\') return 0;
    return 1;
}

static int wl_pfs_api_available(void)
{
    const uint32_t required =
        (uint32_t)(offsetof(TitanAppApiV1, pfs_status) + sizeof(g_wle.api->pfs_status));
    return g_wle.api != 0 && g_wle.api->struct_size >= required &&
        g_wle.api->pfs_available != 0 && g_wle.api->pfs_mount != 0 &&
        g_wle.api->pfs_unmount != 0 && g_wle.api->pfs_status != 0;
}

static int wl_pfs_slot_from_root(const char *root, uint32_t *slot)
{
    if (root == 0 || slot == 0 || wl_strlen(root) != 6u ||
        root[0] != 'p' || root[1] != 'f' || root[2] != 's' ||
        root[3] < '0' || root[3] > '3' || root[4] != ':' || root[5] != '/') return 0;
    *slot = (uint32_t)(root[3] - '0');
    return 1;
}

static int wl_pfs_first_free_slot(uint32_t *slot)
{
    uint32_t i;
    TitanAppPfsMountStatus status;
    if (!wl_pfs_api_available() || slot == 0) return 0;
    for (i = 0u; i < 4u; ++i)
    {
        if (!g_wle.api->pfs_status(i, &status) || status.state == TITAN_APP_PFS_STATE_OFFLINE)
        { *slot = i; return 1; }
    }
    return 0;
}

static void wl_pfs_action(void)
{
    uint32_t slot;
    if (g_wle.vmc_view_active || g_wle.path[0] != '\0')
    { wl_copy(g_wle.status, sizeof(g_wle.status), "PFS: volte a lista de dispositivos"); return; }
    if (!wl_pfs_api_available())
    { wl_copy(g_wle.status, sizeof(g_wle.status), "Runtime Titan sem API PFS"); return; }
    if (g_wle.entry_count != 0u && g_wle.selected < g_wle.entry_count &&
        wl_pfs_slot_from_root(g_wle.entries[g_wle.selected].name, &slot))
    {
        if (g_wle.api->pfs_unmount(slot))
        { wl_copy(g_wle.status, sizeof(g_wle.status), "PFS desmontado pelo Titan"); g_wle.refresh_pending = 1u; }
        else wl_copy(g_wle.status, sizeof(g_wle.status), "PFS nao pertence a esta sessao ou falhou ao desmontar");
        return;
    }
    if (!g_wle.api->pfs_available())
    { wl_copy(g_wle.status, sizeof(g_wle.status), "Driver/provider PFS indisponivel neste Titan"); return; }
    if (!wl_pfs_first_free_slot(&slot))
    { wl_copy(g_wle.status, sizeof(g_wle.status), "Nenhum slot PFS livre"); return; }
    g_wle.text_target[0] = (char)('0' + slot);
    g_wle.text_target[1] = '\0';
    if (g_wle.api->text_input_open("Montar PFS (hdd0:+PARTICAO)", "hdd0:+", 127u))
        g_wle.text_action = WLE_TEXT_PFS_MOUNT;
}

static void wl_poll_text_input(void)
{
    int result;
    char target[WLE_PATH_CAPACITY];
    if (g_wle.text_action == WLE_TEXT_NONE) return;
    result = g_wle.api->text_input_poll(g_wle.text_value, sizeof(g_wle.text_value));
    if (result == TITAN_APP_TEXT_INPUT_PENDING) return;
    if (result == TITAN_APP_TEXT_INPUT_CANCELLED)
    {
        g_wle.text_action = WLE_TEXT_NONE;
        wl_copy(g_wle.status, sizeof(g_wle.status), "Entrada cancelada");
        return;
    }
    if (g_wle.text_action == WLE_TEXT_EDIT_FILE)
    {
        if (!wl_publish_text_edit(g_wle.text_value))
            wl_copy(g_wle.status, sizeof(g_wle.status), "Falha ao publicar edicao; original preservado");
        else
        {
            wl_copy(g_wle.status, sizeof(g_wle.status), "Texto salvo atomicamente");
            (void)g_wle.api->notify(TITAN_APP_NOTIFICATION_SUCCESS, "wLaunchELF", "Texto salvo");
        }
        g_wle.text_action = WLE_TEXT_NONE;
        return;
    }
    if (g_wle.text_action == WLE_TEXT_PFS_MOUNT)
    {
        TitanAppPfsMountStatus status;
        uint32_t slot = (g_wle.text_target[0] >= '0' && g_wle.text_target[0] <= '3') ?
            (uint32_t)(g_wle.text_target[0] - '0') : 4u;
        if (slot >= 4u || !wl_pfs_api_available() ||
            !g_wle.api->pfs_mount(g_wle.text_value, slot,
                TITAN_APP_PFS_CAP_READ | TITAN_APP_PFS_CAP_WRITE, &status))
            wl_copy(g_wle.status, sizeof(g_wle.status), "Falha ao montar PFS pelo Titan");
        else
        {
            wl_copy(g_wle.status, sizeof(g_wle.status), "PFS montado; volume adicionado");
            g_wle.refresh_pending = 1u;
        }
        g_wle.text_action = WLE_TEXT_NONE;
        return;
    }
    if (!wl_name_safe(g_wle.text_value))
    {
        g_wle.text_action = WLE_TEXT_NONE;
        wl_copy(g_wle.status, sizeof(g_wle.status), "Nome invalido");
        return;
    }
    wl_copy(target, sizeof(target), g_wle.path);
    if (!wl_append(target, sizeof(target), g_wle.text_value))
    {
        g_wle.text_action = WLE_TEXT_NONE;
        return;
    }
    if (g_wle.text_action == WLE_TEXT_RENAME)
    {
        if (!g_wle.api->storage_rename(g_wle.text_target, target))
            wl_copy(g_wle.status, sizeof(g_wle.status), "Falha ao renomear");
        else
            wl_copy(g_wle.status, sizeof(g_wle.status), "Item renomeado");
    }
    else
    {
        if (!g_wle.api->storage_mkdir(target))
            wl_copy(g_wle.status, sizeof(g_wle.status), "Falha ao criar pasta");
        else
            wl_copy(g_wle.status, sizeof(g_wle.status), "Pasta criada");
    }
    g_wle.text_action = WLE_TEXT_NONE;
    g_wle.refresh_pending = 1u;
}

static uint32_t wl_udiv32(uint32_t numerator, uint32_t denominator)
{
    uint32_t quotient = 0u;
    uint32_t remainder = 0u;
    int bit;
    if (denominator == 0u) return 0u;
    for (bit = 31; bit >= 0; bit--)
    {
        remainder = (remainder << 1) | ((numerator >> bit) & 1u);
        if (remainder >= denominator)
        {
            remainder -= denominator;
            quotient |= (1u << bit);
        }
    }
    return quotient;
}

static uint32_t wl_copy_percent(void)
{
    uint64_t done = g_wle.copy_offset;
    uint64_t total = g_wle.copy_total;
    if (total == 0u) return 0u;
    while (total > 0xffffffffu)
    {
        total >>= 1u;
        done >>= 1u;
    }
    if (done > total) done = total;
    if ((uint32_t)total == 0u) return 0u;
    if ((uint32_t)done > 0x028f5c28u)
    {
        done >>= 7u;
        total >>= 7u;
    }
    return wl_udiv32((uint32_t)done * 100u, (uint32_t)total);
}

#define WLE_INIT_ERROR_INVALID_SESSION (-1)
#define WLE_INIT_ERROR_INCOMPATIBLE_RUNTIME (-2)

int titan_app_init(const TitanAppApiV1 *api, const char *app_root)
{
    uint32_t i;
    const uint32_t legacy_required_size =
        (uint32_t)(offsetof(TitanAppApiV1, launch_elf) + sizeof(api->launch_elf));
    const uint32_t required_size =
        (uint32_t)(offsetof(TitanAppApiV1, vmc_read_active_file) + sizeof(api->vmc_read_active_file));
    (void)legacy_required_size;
    (void)app_root;
    if (api == 0 || api->abi_version != TITAN_APP_API_VERSION)
        return WLE_INIT_ERROR_INVALID_SESSION;
    if (api->struct_size < required_size)
        return WLE_INIT_ERROR_INCOMPATIBLE_RUNTIME;
    if (api->storage_list == 0 || api->storage_stat == 0 ||
        api->storage_read == 0 || api->storage_write == 0 ||
        api->storage_remove == 0 || api->launch_elf == 0 ||
        api->storage_list_volumes == 0 || api->storage_commit_replace == 0 ||
        api->text_input_open == 0 || api->text_input_poll == 0 ||
        api->media_present_image_file == 0 || api->vmc_list_active_root == 0 ||
        api->vmc_list_active_path == 0 || api->vmc_read_active_file == 0)
        return WLE_INIT_ERROR_INCOMPATIBLE_RUNTIME;
    for (i = 0u; i < sizeof(g_wle); i++) ((uint8_t *)&g_wle)[i] = 0u;
    g_wle.api = api;
    g_wle.refresh_pending = 1u;
    wl_copy(g_wle.status, sizeof(g_wle.status), "Inicializando wLaunchELF para Titan");
    return 0;
}

static void wl_toggle_ftp(void)
{
    TitanAppFtpStatus status;
    if (g_wle.api == 0 || g_wle.path[0] == '\0')
    { wl_copy(g_wle.status,sizeof(g_wle.status),"Entre em um volume/diretorio antes de iniciar FTP"); return; }
    if (g_wle.ftp_enabled)
    {
        g_wle.api->ftp_stop(); g_wle.ftp_enabled=0u;
        wl_copy(g_wle.status,sizeof(g_wle.status),"FTP Titan encerrado"); return;
    }
    if (!g_wle.api->ftp_start(g_wle.path,2121u))
    { wl_copy(g_wle.status,sizeof(g_wle.status),"Nao foi possivel iniciar FTP Titan"); return; }
    g_wle.ftp_enabled=1u;
    if (g_wle.api->ftp_status(&status)) wl_copy(g_wle.status,sizeof(g_wle.status),"FTP Titan ativo na porta 2121  L3 parar");
}

void titan_app_update(const TitanAppInput *input, uint32_t delta_ms)
{
    uint16_t pressed;
    (void)delta_ms;
    if (g_wle.api == 0) return;
    if (g_wle.refresh_pending) wl_refresh();
    if (g_wle.image_viewer_active)
    {
        uint16_t image_pressed = input != 0 ? input->pressed : 0u;
        if ((image_pressed & TITAN_APP_BUTTON_CIRCLE) != 0u)
        {
            g_wle.image_viewer_active = 0u;
            wl_copy(g_wle.status, sizeof(g_wle.status), "X abrir  SELECT acoes  O voltar");
        }
        return;
    }
    if (g_wle.text_action != WLE_TEXT_NONE)
    {
        wl_poll_text_input();
        return;
    }
    if (g_wle.vmc_export_active)
    {
        wl_vmc_export_step();
        return;
    }
    if (g_wle.copy_active)
    {
        wl_copy_step();
        return;
    }
    if (g_wle.recursive_active)
    {
        wl_recursive_step();
        return;
    }
    pressed = input != 0 ? input->pressed : 0u;
    if (g_wle.viewer_active)
    {
        if ((pressed & TITAN_APP_BUTTON_CIRCLE) != 0u)
        {
            g_wle.viewer_active = 0u;
            g_wle.viewer_line = 0u;
            wl_copy(g_wle.status, sizeof(g_wle.status), "X abrir  SELECT acoes  O voltar");
        }
        else if ((pressed & TITAN_APP_BUTTON_UP) != 0u && g_wle.viewer_line != 0u)
            g_wle.viewer_line--;
        else if ((pressed & TITAN_APP_BUTTON_DOWN) != 0u)
            g_wle.viewer_line++;
        else if ((pressed & TITAN_APP_BUTTON_SQUARE) != 0u)
            wl_begin_text_edit();
        return;
    }
    if (g_wle.action_menu)
    {
        if ((pressed & TITAN_APP_BUTTON_UP) != 0u)
            g_wle.action_selected = g_wle.action_selected == 0u ? WLE_ACTION_COUNT - 1u : g_wle.action_selected - 1u;
        if ((pressed & TITAN_APP_BUTTON_DOWN) != 0u)
            g_wle.action_selected = (g_wle.action_selected + 1u) % WLE_ACTION_COUNT;
        if ((pressed & TITAN_APP_BUTTON_CIRCLE) != 0u) g_wle.action_menu = 0u;
        if ((pressed & TITAN_APP_BUTTON_CROSS) != 0u)
        {
            if (g_wle.action_selected == WLE_ACTION_COPY - 1u) wl_mark_copy();
            else if (g_wle.action_selected == WLE_ACTION_MOVE - 1u) wl_mark_move();
            else if (g_wle.action_selected == WLE_ACTION_RENAME - 1u) wl_begin_rename();
            else if (g_wle.action_selected == WLE_ACTION_NEW_FOLDER - 1u) wl_begin_new_folder();
            else wl_delete_selected();
            g_wle.action_menu = 0u;
        }
        return;
    }
    if ((pressed & TITAN_APP_BUTTON_R2) != 0u)
    {
        g_wle.vmc_view_active = g_wle.vmc_view_active ? 0u : 1u;
        g_wle.vmc_path[0] = '\0';
        g_wle.refresh_pending = 1u;
        return;
    }
    if ((pressed & TITAN_APP_BUTTON_UP) != 0u && g_wle.entry_count != 0u)
    {
        if (g_wle.selected == 0u) g_wle.selected = g_wle.entry_count - 1u;
        else g_wle.selected--;
    }
    if ((pressed & TITAN_APP_BUTTON_DOWN) != 0u && g_wle.entry_count != 0u)
        g_wle.selected = (g_wle.selected + 1u) % g_wle.entry_count;
    if (g_wle.selected < g_wle.scroll) g_wle.scroll = g_wle.selected;
    if (g_wle.selected >= g_wle.scroll + WLE_VISIBLE_ROWS)
        g_wle.scroll = g_wle.selected - WLE_VISIBLE_ROWS + 1u;
    if ((pressed & TITAN_APP_BUTTON_CROSS) != 0u) wl_open_selected();
    if ((pressed & TITAN_APP_BUTTON_CIRCLE) != 0u)
    {
        if (g_wle.vmc_view_active)
        {
            if (g_wle.vmc_path[0] == '\0') g_wle.vmc_view_active = 0u;
            else wl_vmc_parent();
            g_wle.refresh_pending = 1u;
        }
        else if (g_wle.path[0] == '\0') g_wle.api->request_exit();
        else wl_parent();
    }
    if ((pressed & TITAN_APP_BUTTON_SQUARE) != 0u)
    {
        if (g_wle.vmc_view_active) wl_begin_vmc_export();
        else wl_mark_copy();
    }
    if ((pressed & TITAN_APP_BUTTON_TRIANGLE) != 0u && !g_wle.vmc_view_active) wl_begin_paste();
    if ((pressed & TITAN_APP_BUTTON_SELECT) != 0u && !g_wle.vmc_view_active && g_wle.path[0] != '\0' && g_wle.entry_count != 0u)
    {
        g_wle.action_menu = 1u;
        g_wle.action_selected = 0u;
    }
    if ((pressed & TITAN_APP_BUTTON_R1) != 0u) wl_pfs_action();
    if ((pressed & TITAN_APP_BUTTON_START) != 0u) g_wle.refresh_pending = 1u;
    if ((pressed & TITAN_APP_BUTTON_L3) != 0u && !g_wle.vmc_view_active) wl_toggle_ftp();
}

static void wl_draw_image_viewer(void)
{
    g_wle.api->draw_panel(24.0f, 22.0f, 806.0f, 404.0f, 184u);
    g_wle.api->draw_rect(36.0f, 34.0f, 5.0f, 350.0f, 222,43,82,220);
    g_wle.api->draw_text_shadow(48.0f, 43.0f, 0.82f, "wLaunchELF Image Viewer", 248,248,252,255);
    g_wle.api->draw_text(48.0f, 70.0f, 0.46f, g_wle.viewer_path, 190,195,207,255);
    g_wle.api->media_draw(62.0f, 94.0f, 730.0f, 268.0f, 255u);
    g_wle.api->draw_text(48.0f, 385.0f, 0.48f, g_wle.status, 194,198,208,255);
}

static void wl_draw_text_viewer(void)
{
    uint32_t offset = 0u;
    uint32_t line = 0u;
    uint32_t drawn = 0u;
    char slice[104];
    if (g_wle.viewer_buffer == 0) return;
    g_wle.api->draw_panel(24.0f, 22.0f, 806.0f, 404.0f, 184u);
    g_wle.api->draw_rect(36.0f, 34.0f, 5.0f, 350.0f, 222,43,82,220);
    g_wle.api->draw_text_shadow(48.0f, 43.0f, 0.82f, "wLaunchELF Text Viewer", 248,248,252,255);
    g_wle.api->draw_text(48.0f, 70.0f, 0.46f, g_wle.viewer_path, 190,195,207,255);
    while (offset < g_wle.viewer_size && drawn < WLE_TEXT_VISIBLE_LINES)
    {
        uint32_t pos = 0u;
        uint32_t start = offset;
        while (offset < g_wle.viewer_size && g_wle.viewer_buffer[offset] != '\n' && pos + 1u < sizeof(slice))
        {
            uint8_t c = g_wle.viewer_buffer[offset++];
            if (c != '\r') slice[pos++] = (char)(c == '\t' ? ' ' : c);
        }
        slice[pos] = '\0';
        while (offset < g_wle.viewer_size && g_wle.viewer_buffer[offset] != '\n') offset++;
        if (offset < g_wle.viewer_size && g_wle.viewer_buffer[offset] == '\n') offset++;
        if (line >= g_wle.viewer_line)
        {
            g_wle.api->draw_text(52.0f, 104.0f + (float)drawn * 21.0f, 0.46f, slice, 235,237,243,255);
            drawn++;
        }
        line++;
        if (offset == start) break;
    }
    if (g_wle.viewer_line >= line && line != 0u) g_wle.viewer_line = line - 1u;
    g_wle.api->draw_text(48.0f, 385.0f, 0.48f, g_wle.status, 194,198,208,255);
}

void titan_app_draw(void)
{
    uint32_t row;
    char size_text[32];
    if (g_wle.api == 0) return;
    if (g_wle.image_viewer_active)
    {
        wl_draw_image_viewer();
        return;
    }
    if (g_wle.viewer_active)
    {
        wl_draw_text_viewer();
        return;
    }
    g_wle.api->draw_panel(24.0f, 22.0f, 806.0f, 404.0f, 168u);
    /* Hardware-visible canary: unlike text, this primitive has no app-owned
     * pointer argument. If the module is drawing at all, the user must see this
     * Magenta rail; any later rejected text command is then reported by Titan's
     * dedicated system-error popup instead of presenting an unexplained black
     * window. */
    g_wle.api->draw_rect(36.0f, 34.0f, 5.0f, 350.0f, 222,43,82,220);
    g_wle.api->draw_text_shadow(48.0f, 43.0f, 0.86f, "wLaunchELF", 248,248,252,255);
    g_wle.api->draw_text(48.0f, 72.0f, 0.58f,
        g_wle.vmc_view_active ? (g_wle.vmc_path[0] != '\0' ? g_wle.vmc_path : "VMC ativa (Titan read-only)") : (g_wle.path[0] != '\0' ? g_wle.path : "Dispositivos"), 202,205,214,255);
    for (row = 0u; row < WLE_VISIBLE_ROWS; row++)
    {
        uint32_t index = g_wle.scroll + row;
        float y = 108.0f + (float)row * 26.0f;
        TitanAppStorageEntry *entry;
        uint32_t mb;
        uint32_t pos;
        if (index >= g_wle.entry_count) break;
        entry = &g_wle.entries[index];
        if (index == g_wle.selected)
            g_wle.api->draw_rect(42.0f, y - 4.0f, 754.0f, 23.0f, 222,43,82,78);
        g_wle.api->draw_text(52.0f, y, 0.58f, entry->name, 240,242,248,255);
        if ((entry->flags & TITAN_APP_STORAGE_ENTRY_DIRECTORY) != 0u)
            g_wle.api->draw_text(668.0f, y, 0.50f, "<DIR>", 184,189,201,255);
        else
        {
            mb = (uint32_t)(entry->size >> 20u);
            pos = 0u;
            if (mb >= 1000u) size_text[pos++] = '0' + (char)((mb / 1000u) % 10u);
            if (mb >= 100u) size_text[pos++] = '0' + (char)((mb / 100u) % 10u);
            if (mb >= 10u) size_text[pos++] = '0' + (char)((mb / 10u) % 10u);
            size_text[pos++] = '0' + (char)(mb % 10u);
            size_text[pos++] = ' ';
            size_text[pos++] = 'M'; size_text[pos++] = 'B'; size_text[pos] = '\0';
            g_wle.api->draw_text(686.0f, y, 0.50f, size_text, 184,189,201,255);
        }
    }
    g_wle.api->draw_text(48.0f, 385.0f, 0.50f, g_wle.status, 194,198,208,255);
    if (g_wle.copy_active)
    {
        g_wle.api->draw_progress(48.0f, 404.0f, 728.0f, 8.0f, wl_copy_percent(), 255u);
    }
    else if (g_wle.action_menu)
    {
        static const char *const labels[WLE_ACTION_COUNT] = {"Copiar","Mover","Renomear","Nova pasta","Excluir"};
        uint32_t action;
        g_wle.api->draw_panel(565.0f, 238.0f, 230.0f, 166.0f, 245u);
        for (action = 0u; action < WLE_ACTION_COUNT; action++)
        {
            float ay = 250.0f + (float)action * 28.0f;
            if (g_wle.action_selected == action)
                g_wle.api->draw_rect(580.0f, ay - 4.0f, 198.0f, 24.0f, 222,43,82,90);
            g_wle.api->draw_text(590.0f, ay, 0.52f, labels[action], 245,245,248,255);
        }
    }
}

void titan_app_shutdown(void)
{
    if (g_wle.api != 0 && g_wle.ftp_enabled) g_wle.api->ftp_stop();
    g_wle.ftp_enabled = 0u;
    if (g_wle.api != 0 && g_wle.copy_active && g_wle.copy_temporary[0] != '\0')
        (void)g_wle.api->storage_remove(g_wle.copy_temporary);
    if (g_wle.api != 0 && g_wle.vmc_export_active && g_wle.vmc_export_temporary[0] != '\0')
        (void)g_wle.api->storage_remove(g_wle.vmc_export_temporary);
    if (g_wle.api != 0 && g_wle.copy_buffer != 0)
        g_wle.api->release(g_wle.copy_buffer);
    if (g_wle.api != 0 && g_wle.viewer_buffer != 0)
        g_wle.api->release(g_wle.viewer_buffer);
    g_wle.api = 0;
    g_wle.copy_buffer = 0;
    g_wle.viewer_buffer = 0;
    g_wle.recursive_active = 0u;
    g_wle.recursive_depth = 0u;
}

#ifdef WLE_HOST_TEST
const char *wle_test_path(void) { return g_wle.path; }
uint32_t wle_test_entry_count(void) { return g_wle.entry_count; }
uint32_t wle_test_selected(void) { return g_wle.selected; }
uint32_t wle_test_viewer_active(void) { return g_wle.viewer_active; }
uint32_t wle_test_image_viewer_active(void) { return g_wle.image_viewer_active; }
uint32_t wle_test_viewer_size(void) { return g_wle.viewer_size; }
const char *wle_test_viewer_path(void) { return g_wle.viewer_path; }
uint32_t wle_test_vmc_view_active(void) { return g_wle.vmc_view_active; }
const char *wle_test_vmc_path(void) { return g_wle.vmc_path; }
uint32_t wle_test_vmc_export_active(void) { return g_wle.vmc_export_active; }
const char *wle_test_vmc_export_destination(void) { return g_wle.vmc_export_destination; }
uint32_t wle_test_recursive_active(void) { return g_wle.recursive_active; }
uint32_t wle_test_recursive_delete_phase(void) { return g_wle.recursive_delete_phase; }
int wle_test_start_recursive(const char *source, const char *destination_parent, int move)
{
    wl_copy(g_wle.copy_source, sizeof(g_wle.copy_source), source);
    wl_copy(g_wle.path, sizeof(g_wle.path), destination_parent);
    g_wle.copy_source_directory = 1u;
    g_wle.copy_marked = 1u;
    g_wle.copy_move = move ? 1u : 0u;
    wl_begin_paste();
    return g_wle.recursive_active ? 1 : 0;
}
#endif
