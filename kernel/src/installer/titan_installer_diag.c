#include "titan_installer_diag.h"
#include "titan_time.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TITAN_INSTALLER_DIAG_BUFFER_SIZE (128u * 1024u)
#define TITAN_INSTALLER_DIAG_LINE_SIZE 512u
#define TITAN_INSTALLER_DIAG_PATH_SIZE 256u
#define TITAN_INSTALLER_DIAG_SLOW_IO_MS 8u
#define TITAN_INSTALLER_DIAG_SLOW_IO_LOG_MS 50u
#define TITAN_INSTALLER_DIAG_PERF_INTERVAL_MS 5000u

/* Circular byte buffer. Old complete lines are discarded when full so the
 * failure tail is always retained. */
static char g_diag_buffer[TITAN_INSTALLER_DIAG_BUFFER_SIZE];
static unsigned int g_diag_head;
static unsigned int g_diag_tail;
static unsigned int g_diag_used;
static unsigned int g_diag_dropped_lines;

static char g_diag_version[32];
static char g_diag_executable[TITAN_INSTALLER_DIAG_PATH_SIZE];
static char g_diag_usb_prefix[16];
static char g_diag_log_path[TITAN_INSTALLER_DIAG_PATH_SIZE];
static char g_diag_prev_path[TITAN_INSTALLER_DIAG_PATH_SIZE];
static char g_diag_mc_path[TITAN_INSTALLER_DIAG_PATH_SIZE];
static int g_diag_memory_card;
static int g_diag_bound;
static uint64_t g_diag_session_start_ms;

static unsigned int g_diag_frame_number;
static uint64_t g_diag_frame_io_total_ms;
static uint64_t g_diag_frame_io_max_ms;
static unsigned int g_diag_frame_io_calls;
static unsigned long long g_diag_frame_io_bytes;
static char g_diag_frame_io_op[40];
static char g_diag_frame_io_path[160];
static int g_diag_frame_io_result;
static int g_diag_frame_io_errno;
static uint64_t g_diag_frame_cpu_max_ms;
static char g_diag_frame_cpu_op[40];
static unsigned int g_diag_frame_cpu_bytes;

static unsigned int g_diag_context_entry;
static unsigned int g_diag_context_count;
static unsigned int g_diag_context_progress;
static char g_diag_context_phase[48];
static char g_diag_context_path[192];

static unsigned long long g_diag_total_read_bytes;
static unsigned long long g_diag_total_write_bytes;
static unsigned long long g_diag_total_io_bytes;
static unsigned int g_diag_total_io_calls;
static uint64_t g_diag_worst_io_ms;
static char g_diag_worst_io_op[40];
static char g_diag_worst_io_path[160];

static unsigned int g_diag_total_frames;
static unsigned int g_diag_stutter_frames;
static uint64_t g_diag_worst_frame_ms;
static uint64_t g_diag_worst_install_ms;
static uint64_t g_diag_worst_render_ms;

static uint64_t g_diag_perf_window_start_ms;
static unsigned int g_diag_perf_frames;
static unsigned int g_diag_perf_stutters;
static unsigned long long g_diag_perf_io_bytes;
static uint64_t g_diag_perf_install_ms;
static uint64_t g_diag_perf_render_ms;
static uint64_t g_diag_perf_worst_frame_ms;
static uint64_t g_diag_perf_worst_io_ms;

/* Repetitive frame stalls are aggregated so a long install cannot evict the
 * beginning of the diagnostic log. Severe individual stalls are still logged. */
static char g_diag_stutter_agg_cause[64];
static char g_diag_stutter_agg_phase[48];
static char g_diag_stutter_agg_op[40];
static char g_diag_stutter_agg_path[160];
static unsigned int g_diag_stutter_agg_count;
static uint64_t g_diag_stutter_agg_sum_ms;
static uint64_t g_diag_stutter_agg_min_ms;
static uint64_t g_diag_stutter_agg_max_ms;

static char g_diag_async_agg_op[40];
static char g_diag_async_agg_phase[48];
static char g_diag_async_agg_path[160];
static unsigned int g_diag_async_agg_count;
static unsigned long long g_diag_async_agg_bytes;
static uint64_t g_diag_async_agg_sum_ms;
static uint64_t g_diag_async_agg_min_ms;
static uint64_t g_diag_async_agg_max_ms;

static void diag_copy_string(char *dst, unsigned int cap, const char *src)
{
    unsigned int i = 0u;
    if (dst == 0 || cap == 0u) return;
    if (src == 0) src = "";
    while (i + 1u < cap && src[i] != '\0') { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void diag_drop_oldest_line(void)
{
    if (g_diag_used == 0u) return;
    while (g_diag_used > 0u) {
        char value = g_diag_buffer[g_diag_tail];
        g_diag_tail = (g_diag_tail + 1u) % TITAN_INSTALLER_DIAG_BUFFER_SIZE;
        g_diag_used--;
        if (value == '\n') break;
    }
    g_diag_dropped_lines++;
}

static void diag_append_bytes(const char *data, unsigned int length)
{
    unsigned int i;
    if (data == 0 || length == 0u) return;
    if (length >= TITAN_INSTALLER_DIAG_BUFFER_SIZE) {
        data += length - (TITAN_INSTALLER_DIAG_BUFFER_SIZE - 1u);
        length = TITAN_INSTALLER_DIAG_BUFFER_SIZE - 1u;
    }
    while (TITAN_INSTALLER_DIAG_BUFFER_SIZE - g_diag_used < length)
        diag_drop_oldest_line();
    for (i = 0u; i < length; i++) {
        g_diag_buffer[g_diag_head] = data[i];
        g_diag_head = (g_diag_head + 1u) % TITAN_INSTALLER_DIAG_BUFFER_SIZE;
    }
    g_diag_used += length;
}

static void diag_append_line(const char *level, const char *event, const char *message)
{
    char line[TITAN_INSTALLER_DIAG_LINE_SIZE];
    uint64_t now_ms = TitanTime_NowMilliseconds();
    unsigned long long elapsed = now_ms >= g_diag_session_start_ms
        ? (unsigned long long)(now_ms - g_diag_session_start_ms)
        : 0ull;
    int length = snprintf(
        line,
        sizeof(line),
        "[%09llu ms] %-5s %-24s %s\n",
        elapsed,
        level ? level : "INFO",
        event ? event : "EVENT",
        message ? message : ""
    );
    if (length <= 0) return;
    if ((unsigned int)length >= sizeof(line)) length = (int)sizeof(line) - 1;
    diag_append_bytes(line, (unsigned int)length);
}

void TitanInstallerDiag_Logf(const char *level, const char *event, const char *format, ...)
{
    char message[360];
    va_list args;
    if (format == 0) {
        diag_append_line(level, event, "");
        return;
    }
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    message[sizeof(message) - 1u] = '\0';
    diag_append_line(level, event, message);
}

static int diag_write_all(int fd, const void *buffer, unsigned int length)
{
    const unsigned char *data = (const unsigned char *)buffer;
    unsigned int done = 0u;
    while (done < length) {
        int result = write(fd, data + done, length - done);
        if (result <= 0) return 0;
        done += (unsigned int)result;
    }
    return 1;
}

static int diag_copy_file(const char *source, const char *destination)
{
    unsigned char buffer[4096];
    int src;
    int dst;
    int got;
    if (source == 0 || destination == 0) return 0;
    src = open(source, O_RDONLY);
    if (src < 0) return 0;
    dst = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dst < 0) { close(src); return 0; }
    for (;;) {
        got = read(src, buffer, sizeof(buffer));
        if (got == 0) break;
        if (got < 0 || !diag_write_all(dst, buffer, (unsigned int)got)) {
            close(src);
            close(dst);
            return 0;
        }
    }
    close(src);
    close(dst);
    return 1;
}

static int diag_write_ring(int fd)
{
    unsigned int first;
    unsigned int second;
    if (g_diag_used == 0u) return 1;
    first = TITAN_INSTALLER_DIAG_BUFFER_SIZE - g_diag_tail;
    if (first > g_diag_used) first = g_diag_used;
    if (first > 0u && !diag_write_all(fd, g_diag_buffer + g_diag_tail, first)) return 0;
    second = g_diag_used - first;
    if (second > 0u && !diag_write_all(fd, g_diag_buffer, second)) return 0;
    return 1;
}

static int diag_write_snapshot_to(const char *path, const char *result_label)
{
    char summary[1400];
    int fd;
    int length;
    if (path == 0 || path[0] == '\0') return 0;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return 0;
    length = snprintf(
        summary,
        sizeof(summary),
        "Project Titan Magenta Edition - Titan Installer Diagnostic Log\n"
        "format=1 installer=%s result=%s\n"
        "executable=%s usb=%s mc=%d\n"
        "frames=%u stutters=%u worst_frame_ms=%llu worst_install_pump_ms=%llu worst_render_ms=%llu\n"
        "io_calls=%u io_bytes=%llu read_bytes=%llu write_bytes=%llu worst_io_ms=%llu worst_io_op=%s worst_io_path=%s\n"
        "context_entry=%u/%u progress=%u phase=%s path=%s\n"
        "ring_dropped_lines=%u\n"
        "--- TRACE ---\n",
        g_diag_version,
        result_label ? result_label : "snapshot",
        g_diag_executable,
        g_diag_usb_prefix,
        g_diag_memory_card,
        g_diag_total_frames,
        g_diag_stutter_frames,
        (unsigned long long)g_diag_worst_frame_ms,
        (unsigned long long)g_diag_worst_install_ms,
        (unsigned long long)g_diag_worst_render_ms,
        g_diag_total_io_calls,
        g_diag_total_io_bytes,
        g_diag_total_read_bytes,
        g_diag_total_write_bytes,
        (unsigned long long)g_diag_worst_io_ms,
        g_diag_worst_io_op,
        g_diag_worst_io_path,
        g_diag_context_entry,
        g_diag_context_count,
        g_diag_context_progress,
        g_diag_context_phase,
        g_diag_context_path,
        g_diag_dropped_lines
    );
    if (length <= 0 || !diag_write_all(fd, summary, (unsigned int)length) || !diag_write_ring(fd)) {
        close(fd);
        return 0;
    }
    close(fd);
    return 1;
}

void TitanInstallerDiag_Start(const char *installer_version, const char *executable_path)
{
    memset(g_diag_buffer, 0, sizeof(g_diag_buffer));
    g_diag_head = g_diag_tail = g_diag_used = g_diag_dropped_lines = 0u;
    memset(g_diag_version, 0, sizeof(g_diag_version));
    memset(g_diag_executable, 0, sizeof(g_diag_executable));
    memset(g_diag_usb_prefix, 0, sizeof(g_diag_usb_prefix));
    memset(g_diag_log_path, 0, sizeof(g_diag_log_path));
    memset(g_diag_prev_path, 0, sizeof(g_diag_prev_path));
    memset(g_diag_mc_path, 0, sizeof(g_diag_mc_path));
    memset(g_diag_context_phase, 0, sizeof(g_diag_context_phase));
    memset(g_diag_context_path, 0, sizeof(g_diag_context_path));
    memset(g_diag_worst_io_op, 0, sizeof(g_diag_worst_io_op));
    memset(g_diag_worst_io_path, 0, sizeof(g_diag_worst_io_path));
    g_diag_memory_card = 0;
    g_diag_bound = 0;
    g_diag_session_start_ms = TitanTime_NowMilliseconds();
    g_diag_frame_number = 0u;
    g_diag_total_read_bytes = 0ull;
    g_diag_total_write_bytes = 0ull;
    g_diag_total_io_bytes = 0ull;
    g_diag_total_io_calls = 0u;
    g_diag_worst_io_ms = 0u;
    g_diag_total_frames = 0u;
    g_diag_stutter_frames = 0u;
    g_diag_worst_frame_ms = 0u;
    g_diag_worst_install_ms = 0u;
    g_diag_worst_render_ms = 0u;
    g_diag_perf_window_start_ms = g_diag_session_start_ms;
    g_diag_perf_frames = 0u;
    g_diag_perf_stutters = 0u;
    g_diag_perf_io_bytes = 0ull;
    g_diag_perf_install_ms = 0u;
    g_diag_perf_render_ms = 0u;
    g_diag_perf_worst_frame_ms = 0u;
    g_diag_perf_worst_io_ms = 0u;
    diag_copy_string(g_diag_version, sizeof(g_diag_version), installer_version);
    diag_copy_string(g_diag_executable, sizeof(g_diag_executable), executable_path);
    TitanInstallerDiag_Logf("INFO", "SESSION_START", "installer=%s executable=%s", g_diag_version, g_diag_executable);
}

void TitanInstallerDiag_SetMemoryCard(int memory_card)
{
    g_diag_memory_card = memory_card < 0 ? 0 : memory_card;
    snprintf(g_diag_mc_path, sizeof(g_diag_mc_path), "mc%d:/TITAN/INSTALLER.LOG", g_diag_memory_card);
}

void TitanInstallerDiag_BindStorage(const char *usb_prefix, int memory_card)
{
    char mc_dir[32];
    int rotated = 0;
    diag_copy_string(g_diag_usb_prefix, sizeof(g_diag_usb_prefix), usb_prefix ? usb_prefix : "");
    TitanInstallerDiag_SetMemoryCard(memory_card);
    if (g_diag_usb_prefix[0] != '\0') {
        snprintf(g_diag_log_path, sizeof(g_diag_log_path), "%s/TITAN_INSTALL/INSTALLER.LOG", g_diag_usb_prefix);
        snprintf(g_diag_prev_path, sizeof(g_diag_prev_path), "%s/TITAN_INSTALL/INSTALLER.PREV.LOG", g_diag_usb_prefix);
        rotated = diag_copy_file(g_diag_log_path, g_diag_prev_path);
        g_diag_bound = 1;
    }
    snprintf(mc_dir, sizeof(mc_dir), "mc%d:/TITAN", g_diag_memory_card);
    (void)mkdir(mc_dir, 0777);
    TitanInstallerDiag_Logf(
        "INFO",
        "LOG_STORAGE_BOUND",
        "usb=%s log=%s prev=%s mc_fallback=%s previous_preserved=%d",
        g_diag_usb_prefix,
        g_diag_log_path,
        g_diag_prev_path,
        g_diag_mc_path,
        rotated
    );
}

void TitanInstallerDiag_SetContext(
    unsigned int entry_index,
    unsigned int entry_count,
    const char *phase,
    const char *path,
    unsigned int progress
)
{
    const char *new_phase = phase ? phase : "";
    const char *new_path = path ? path : "";
    int changed =
        entry_index != g_diag_context_entry ||
        strcmp(new_phase, g_diag_context_phase) != 0 ||
        strcmp(new_path, g_diag_context_path) != 0;
    if (changed) {
        TitanInstallerDiag_Logf(
            "INFO",
            "INSTALL_STATE",
            "entry=%u/%u progress=%u phase=%s path=%s",
            entry_index,
            entry_count,
            progress,
            new_phase,
            new_path
        );
    }
    g_diag_context_entry = entry_index;
    g_diag_context_count = entry_count;
    g_diag_context_progress = progress;
    diag_copy_string(g_diag_context_phase, sizeof(g_diag_context_phase), new_phase);
    diag_copy_string(g_diag_context_path, sizeof(g_diag_context_path), new_path);
}

void TitanInstallerDiag_BeginFrame(unsigned int frame_number)
{
    g_diag_frame_number = frame_number;
    g_diag_frame_io_total_ms = 0u;
    g_diag_frame_io_max_ms = 0u;
    g_diag_frame_io_calls = 0u;
    g_diag_frame_io_bytes = 0ull;
    g_diag_frame_io_op[0] = '\0';
    g_diag_frame_io_path[0] = '\0';
    g_diag_frame_io_result = 0;
    g_diag_frame_io_errno = 0;
    g_diag_frame_cpu_max_ms = 0u;
    g_diag_frame_cpu_op[0] = '\0';
    g_diag_frame_cpu_bytes = 0u;
}

static void diag_flush_stutter_aggregate(void)
{
    if (g_diag_stutter_agg_count == 0u) return;
    TitanInstallerDiag_Logf(
        "INFO", "STUTTER_AGGREGATE",
        "count=%u min_ms=%llu avg_ms=%llu max_ms=%llu cause=%s phase=%s io_op=%s io_path=%s",
        g_diag_stutter_agg_count,
        (unsigned long long)g_diag_stutter_agg_min_ms,
        (unsigned long long)(g_diag_stutter_agg_sum_ms / g_diag_stutter_agg_count),
        (unsigned long long)g_diag_stutter_agg_max_ms,
        g_diag_stutter_agg_cause,
        g_diag_stutter_agg_phase,
        g_diag_stutter_agg_op,
        g_diag_stutter_agg_path
    );
    g_diag_stutter_agg_count = 0u;
    g_diag_stutter_agg_sum_ms = 0u;
    g_diag_stutter_agg_min_ms = 0u;
    g_diag_stutter_agg_max_ms = 0u;
    g_diag_stutter_agg_cause[0] = '\0';
    g_diag_stutter_agg_phase[0] = '\0';
    g_diag_stutter_agg_op[0] = '\0';
    g_diag_stutter_agg_path[0] = '\0';
}

static void diag_accumulate_stutter(const char *cause, const char *phase, const char *op,
                                    const char *path, uint64_t frame_ms)
{
    const char *c = cause ? cause : "";
    const char *ph = phase ? phase : "";
    const char *io = op ? op : "";
    const char *pa = path ? path : "";
    int same = g_diag_stutter_agg_count > 0u &&
        strcmp(c, g_diag_stutter_agg_cause) == 0 &&
        strcmp(ph, g_diag_stutter_agg_phase) == 0 &&
        strcmp(io, g_diag_stutter_agg_op) == 0 &&
        strcmp(pa, g_diag_stutter_agg_path) == 0;
    if (!same) {
        diag_flush_stutter_aggregate();
        diag_copy_string(g_diag_stutter_agg_cause, sizeof(g_diag_stutter_agg_cause), c);
        diag_copy_string(g_diag_stutter_agg_phase, sizeof(g_diag_stutter_agg_phase), ph);
        diag_copy_string(g_diag_stutter_agg_op, sizeof(g_diag_stutter_agg_op), io);
        diag_copy_string(g_diag_stutter_agg_path, sizeof(g_diag_stutter_agg_path), pa);
        g_diag_stutter_agg_min_ms = frame_ms;
        g_diag_stutter_agg_max_ms = frame_ms;
    }
    g_diag_stutter_agg_count++;
    g_diag_stutter_agg_sum_ms += frame_ms;
    if (frame_ms < g_diag_stutter_agg_min_ms) g_diag_stutter_agg_min_ms = frame_ms;
    if (frame_ms > g_diag_stutter_agg_max_ms) g_diag_stutter_agg_max_ms = frame_ms;
}

static void diag_flush_async_aggregate(void)
{
    if (g_diag_async_agg_count == 0u) return;
    TitanInstallerDiag_Logf(
        "INFO", "ASYNC_IO_AGGREGATE",
        "count=%u bytes=%llu min_ms=%llu avg_ms=%llu max_ms=%llu phase=%s op=%s path=%s",
        g_diag_async_agg_count,
        g_diag_async_agg_bytes,
        (unsigned long long)g_diag_async_agg_min_ms,
        (unsigned long long)(g_diag_async_agg_sum_ms / g_diag_async_agg_count),
        (unsigned long long)g_diag_async_agg_max_ms,
        g_diag_async_agg_phase,
        g_diag_async_agg_op,
        g_diag_async_agg_path
    );
    g_diag_async_agg_count = 0u;
    g_diag_async_agg_bytes = 0ull;
    g_diag_async_agg_sum_ms = 0u;
    g_diag_async_agg_min_ms = 0u;
    g_diag_async_agg_max_ms = 0u;
    g_diag_async_agg_op[0] = '\0';
    g_diag_async_agg_phase[0] = '\0';
    g_diag_async_agg_path[0] = '\0';
}

static void diag_accumulate_async_io(const char *op, const char *path, unsigned int bytes, uint64_t elapsed_ms)
{
    const char *operation = op ? op : "ASYNC_IO";
    const char *io_path = path ? path : "";
    int same = g_diag_async_agg_count > 0u &&
        strcmp(operation, g_diag_async_agg_op) == 0 &&
        strcmp(g_diag_context_phase, g_diag_async_agg_phase) == 0 &&
        strcmp(io_path, g_diag_async_agg_path) == 0;
    if (!same) {
        diag_flush_async_aggregate();
        diag_copy_string(g_diag_async_agg_op, sizeof(g_diag_async_agg_op), operation);
        diag_copy_string(g_diag_async_agg_phase, sizeof(g_diag_async_agg_phase), g_diag_context_phase);
        diag_copy_string(g_diag_async_agg_path, sizeof(g_diag_async_agg_path), io_path);
        g_diag_async_agg_min_ms = elapsed_ms;
        g_diag_async_agg_max_ms = elapsed_ms;
    }
    g_diag_async_agg_count++;
    g_diag_async_agg_bytes += bytes;
    g_diag_async_agg_sum_ms += elapsed_ms;
    if (elapsed_ms < g_diag_async_agg_min_ms) g_diag_async_agg_min_ms = elapsed_ms;
    if (elapsed_ms > g_diag_async_agg_max_ms) g_diag_async_agg_max_ms = elapsed_ms;
}


static int diag_expected_absent_probe(const char *op, const char *path, int result)
{
    const char *phase = g_diag_context_phase;
    if (result >= 0 || op == 0 || path == 0) return 0;
    /* Creating an already-existing parent is expected on FAT/newlib and some
     * drivers report it with non-POSIX errno values. The subsequent output
     * open is the authoritative failure point. */
    if (strcmp(op, "MKDIR_OPTIONAL") == 0) return 1;
    if (strcmp(phase, "PREPARE_STAGE") == 0 && strstr(op, "OPEN_EXISTING") != 0) return 1;
    if (strcmp(phase, "PREPARE_STAGE") == 0 && strstr(op, "REMOVE_STALE_STAGE") != 0) return 1;
    if (strstr(op, "STAT") == 0) return 0;
    /* INSTALLER.CONSUMED not existing is the normal first-install state. */
    if (strstr(path, "TITAN_INSTALL/INSTALLER.CONSUMED") != 0) return 1;
    if (strcmp(phase, "RECOVER") != 0 &&
        strcmp(phase, "ROLLBACK") != 0 &&
        strcmp(phase, "PREPARE_STAGE") != 0 &&
        strcmp(phase, "PUBLISH_ENTRY") != 0)
        return 0;
    /* During transactional phases STAT is an existence probe. Missing the
     * destination, scratch file or backup is expected; the subsequent open,
     * rename, remove or copy is the authoritative operation. */
    return 1;
}

void TitanInstallerDiag_RecordIo(
    const char *operation,
    const char *path,
    unsigned int requested_bytes,
    int result,
    int error_number,
    uint64_t elapsed_ms
)
{
    const char *op = operation ? operation : "IO";
    int async_op = strncmp(op, "ASYNC_", 6u) == 0;
    unsigned int actual =
        requested_bytes > 0u && result > 0 &&
        (strstr(op, "READ") != 0 || strstr(op, "WRITE") != 0)
            ? (unsigned int)result
            : 0u;
    const char *io_path = path && path[0] ? path : g_diag_context_path;

    /* Async RPC service latency belongs to total throughput diagnostics, but it
     * is NOT main-thread blocking time. Counting it in the completion frame
     * would falsely blame the waves for time spent concurrently on the IOP. */
    if (!async_op) {
        g_diag_frame_io_calls++;
        g_diag_frame_io_total_ms += elapsed_ms;
        g_diag_frame_io_bytes += actual;
        if (elapsed_ms >= g_diag_frame_io_max_ms) {
            g_diag_frame_io_max_ms = elapsed_ms;
            diag_copy_string(g_diag_frame_io_op, sizeof(g_diag_frame_io_op), op);
            diag_copy_string(g_diag_frame_io_path, sizeof(g_diag_frame_io_path), io_path);
            g_diag_frame_io_result = result;
            g_diag_frame_io_errno = error_number;
        }
    }

    g_diag_total_io_calls++;
    g_diag_total_io_bytes += actual;
    g_diag_perf_io_bytes += actual;
    if (strstr(op, "READ") != 0) g_diag_total_read_bytes += actual;
    if (strstr(op, "WRITE") != 0) g_diag_total_write_bytes += actual;
    if (elapsed_ms >= g_diag_worst_io_ms) {
        g_diag_worst_io_ms = elapsed_ms;
        diag_copy_string(g_diag_worst_io_op, sizeof(g_diag_worst_io_op), op);
        diag_copy_string(g_diag_worst_io_path, sizeof(g_diag_worst_io_path), io_path);
    }
    if (elapsed_ms >= g_diag_perf_worst_io_ms) g_diag_perf_worst_io_ms = elapsed_ms;

    if (async_op && requested_bytes > 0u && result == 0) {
        /* A completed READ/WRITE RPC that transfers zero bytes while bytes were
         * requested is not an ordinary latency sample. Preserve it explicitly
         * because it usually means bad access flags, EOF or a backend fault. */
        diag_flush_async_aggregate();
        TitanInstallerDiag_Logf(
            "ERROR", "ASYNC_ZERO_TRANSFER",
            "frame=%u entry=%u/%u phase=%s op=%s path=%s requested=%u result=0 elapsed_ms=%llu frame_blocking=0",
            g_diag_frame_number, g_diag_context_entry, g_diag_context_count,
            g_diag_context_phase, op, io_path ? io_path : "", requested_bytes,
            (unsigned long long)elapsed_ms
        );
    }

    if (async_op && result >= 0) {
        diag_accumulate_async_io(op, io_path, actual, elapsed_ms);
        /* Keep only exceptional individual service-latency samples. Normal
         * repetitive async metadata operations are represented by aggregates. */
        if (elapsed_ms >= 1000u) {
            TitanInstallerDiag_Logf(
                "WARN", "ASYNC_IO_LATENCY",
                "frame=%u entry=%u/%u phase=%s op=%s path=%s requested=%u result=%d elapsed_ms=%llu frame_blocking=0",
                g_diag_frame_number, g_diag_context_entry, g_diag_context_count,
                g_diag_context_phase, op, io_path ? io_path : "", requested_bytes,
                result, (unsigned long long)elapsed_ms
            );
        }
    } else if (!diag_expected_absent_probe(op, io_path, result) &&
               (result < 0 || elapsed_ms >= TITAN_INSTALLER_DIAG_SLOW_IO_LOG_MS)) {
        if (result < 0) diag_flush_async_aggregate();
        TitanInstallerDiag_Logf(
            result < 0 ? "ERROR" : "WARN",
            result < 0 ? "IO_FAILURE" : "SLOW_IO",
            "frame=%u entry=%u/%u phase=%s op=%s path=%s requested=%u result=%d errno=%d elapsed_ms=%llu frame_blocking=1",
            g_diag_frame_number, g_diag_context_entry, g_diag_context_count,
            g_diag_context_phase, op, io_path ? io_path : "", requested_bytes,
            result, error_number, (unsigned long long)elapsed_ms
        );
    }
}

void TitanInstallerDiag_RecordCpuWork(
    const char *operation,
    unsigned int processed_bytes,
    uint64_t elapsed_ms
)
{
    if (elapsed_ms >= g_diag_frame_cpu_max_ms) {
        g_diag_frame_cpu_max_ms = elapsed_ms;
        g_diag_frame_cpu_bytes = processed_bytes;
        diag_copy_string(g_diag_frame_cpu_op, sizeof(g_diag_frame_cpu_op), operation ? operation : "CPU_WORK");
    }
    if (elapsed_ms >= TITAN_INSTALLER_DIAG_SLOW_IO_MS) {
        TitanInstallerDiag_Logf(
            "WARN", "SLOW_CPU_WORK",
            "frame=%u entry=%u/%u phase=%s op=%s bytes=%u elapsed_ms=%llu",
            g_diag_frame_number, g_diag_context_entry, g_diag_context_count,
            g_diag_context_phase, operation ? operation : "CPU_WORK", processed_bytes,
            (unsigned long long)elapsed_ms
        );
    }
}

static const char *diag_stutter_cause(
    uint64_t frame_ms,
    uint64_t input_ms,
    uint64_t install_ms,
    uint64_t render_ms,
    unsigned int target_ms
)
{
    if (g_diag_frame_io_max_ms >= (uint64_t)(target_ms / 2u)) {
        if (frame_ms + 2u >= (uint64_t)(target_ms * 2u) && render_ms >= (uint64_t)(target_ms / 2u))
            return "LIKELY_MISSED_VSYNC_AFTER_IO";
        if (install_ms > 10u)
            return "SYNC_IO_BLOCKED_MAIN_EE_THREAD";
        return "SLOW_FILESYSTEM_RPC";
    }
    if (g_diag_frame_cpu_max_ms >= (uint64_t)(target_ms / 3u))
        return "CRC_OR_CPU_WORK_BLOCKED_MAIN_EE_THREAD";
    if (install_ms > 10u)
        return "INSTALL_METADATA_OVERRAN_BUDGET";
    if (render_ms > (uint64_t)(target_ms + 4u))
        return "RENDER_OR_VSYNC_WAIT";
    if (input_ms > 6u)
        return "INPUT_OR_IOP_SERVICE_STALL";
    return "UNATTRIBUTED_MAIN_THREAD_GAP";
}

void TitanInstallerDiag_EndFrame(
    uint64_t frame_elapsed_ms,
    uint64_t input_elapsed_ms,
    uint64_t install_elapsed_ms,
    uint64_t render_elapsed_ms,
    unsigned int target_frame_ms,
    unsigned int progress,
    const char *phase,
    const char *path
)
{
    uint64_t now_ms = TitanTime_NowMilliseconds();
    unsigned int stutter_threshold;
    int stutter;
    const char *cause;
    unsigned long long window_ms;
    unsigned int fps_x100;
    unsigned long long kib_s;

    if (target_frame_ms < 10u) target_frame_ms = 17u;
    stutter_threshold = target_frame_ms + 5u;
    stutter = frame_elapsed_ms > (uint64_t)stutter_threshold;

    g_diag_total_frames++;
    g_diag_perf_frames++;
    g_diag_perf_install_ms += install_elapsed_ms;
    g_diag_perf_render_ms += render_elapsed_ms;
    if (frame_elapsed_ms > g_diag_worst_frame_ms) g_diag_worst_frame_ms = frame_elapsed_ms;
    if (install_elapsed_ms > g_diag_worst_install_ms) g_diag_worst_install_ms = install_elapsed_ms;
    if (render_elapsed_ms > g_diag_worst_render_ms) g_diag_worst_render_ms = render_elapsed_ms;
    if (frame_elapsed_ms > g_diag_perf_worst_frame_ms) g_diag_perf_worst_frame_ms = frame_elapsed_ms;

    if (stutter) {
        int first_of_group;
        g_diag_stutter_frames++;
        g_diag_perf_stutters++;
        cause = diag_stutter_cause(frame_elapsed_ms, input_elapsed_ms, install_elapsed_ms,
                                   render_elapsed_ms, target_frame_ms);
        first_of_group = g_diag_stutter_agg_count == 0u ||
            strcmp(cause, g_diag_stutter_agg_cause) != 0 ||
            strcmp(phase ? phase : "", g_diag_stutter_agg_phase) != 0 ||
            strcmp(g_diag_frame_io_op, g_diag_stutter_agg_op) != 0 ||
            strcmp(g_diag_frame_io_path, g_diag_stutter_agg_path) != 0;
        diag_accumulate_stutter(cause, phase, g_diag_frame_io_op, g_diag_frame_io_path, frame_elapsed_ms);

        /* Preserve the first exemplar of a group and truly severe stalls. The
         * aggregate retains frequency/min/avg/max without flooding 30k lines. */
        if (first_of_group || frame_elapsed_ms >= 1000u) {
            TitanInstallerDiag_Logf(
                "WARN", "WAVE_STUTTER",
                "frame=%u gap_ms=%llu target_ms=%u progress=%u phase=%s path=%s cause=%s input_ms=%llu install_pump_ms=%llu render_vsync_ms=%llu blocking_io_calls=%u blocking_io_total_ms=%llu blocking_io_max_ms=%llu io_op=%s io_path=%s io_result=%d io_errno=%d cpu_max_ms=%llu cpu_op=%s cpu_bytes=%u",
                g_diag_frame_number, (unsigned long long)frame_elapsed_ms, target_frame_ms,
                progress, phase ? phase : "", path ? path : "", cause,
                (unsigned long long)input_elapsed_ms, (unsigned long long)install_elapsed_ms,
                (unsigned long long)render_elapsed_ms, g_diag_frame_io_calls,
                (unsigned long long)g_diag_frame_io_total_ms, (unsigned long long)g_diag_frame_io_max_ms,
                g_diag_frame_io_op, g_diag_frame_io_path, g_diag_frame_io_result, g_diag_frame_io_errno,
                (unsigned long long)g_diag_frame_cpu_max_ms, g_diag_frame_cpu_op, g_diag_frame_cpu_bytes
            );
        }
    }

    if (now_ms >= g_diag_perf_window_start_ms &&
        now_ms - g_diag_perf_window_start_ms >= TITAN_INSTALLER_DIAG_PERF_INTERVAL_MS) {
        diag_flush_stutter_aggregate();
        diag_flush_async_aggregate();
        window_ms = (unsigned long long)(now_ms - g_diag_perf_window_start_ms);
        fps_x100 = window_ms != 0ull
            ? (unsigned int)(((unsigned long long)g_diag_perf_frames * 100000ull) / window_ms)
            : 0u;
        kib_s = window_ms != 0ull
            ? (g_diag_perf_io_bytes * 1000ull) / (1024ull * window_ms)
            : 0ull;
        TitanInstallerDiag_Logf(
            "INFO", "PERF_WINDOW",
            "window_ms=%llu frames=%u fps=%u.%02u stutters=%u io_kib_s=%llu avg_install_ms=%llu avg_render_ms=%llu worst_frame_ms=%llu worst_io_service_ms=%llu progress=%u phase=%s path=%s",
            window_ms, g_diag_perf_frames, fps_x100 / 100u, fps_x100 % 100u,
            g_diag_perf_stutters, kib_s,
            g_diag_perf_frames ? (unsigned long long)(g_diag_perf_install_ms / g_diag_perf_frames) : 0ull,
            g_diag_perf_frames ? (unsigned long long)(g_diag_perf_render_ms / g_diag_perf_frames) : 0ull,
            (unsigned long long)g_diag_perf_worst_frame_ms,
            (unsigned long long)g_diag_perf_worst_io_ms,
            progress, phase ? phase : "", path ? path : ""
        );
        g_diag_perf_window_start_ms = now_ms;
        g_diag_perf_frames = 0u;
        g_diag_perf_stutters = 0u;
        g_diag_perf_io_bytes = 0ull;
        g_diag_perf_install_ms = 0u;
        g_diag_perf_render_ms = 0u;
        g_diag_perf_worst_frame_ms = 0u;
        g_diag_perf_worst_io_ms = 0u;
    }
}

int TitanInstallerDiag_FlushCheckpoint(const char *label)
{
    /* Checkpoints are RAM-only while installation is active. v2.0.13 wrote the
     * entire diagnostic ring to mass: here and blocked the UI for 3283 ms before
     * the first payload operation. The final/error flush remains persistent. */
    diag_flush_stutter_aggregate();
    diag_flush_async_aggregate();
    TitanInstallerDiag_Logf("INFO", "CHECKPOINT",
                            "label=%s persistence=ram-only",
                            label ? label : "checkpoint");
    return 1;
}

int TitanInstallerDiag_FlushFinal(const char *result_label)
{
    int usb_ok = 0;
    int mc_ok = 0;
    char mc_dir[32];
    diag_flush_stutter_aggregate();
    diag_flush_async_aggregate();
    TitanInstallerDiag_Logf(
        "INFO",
        "SESSION_END",
        "result=%s frames=%u stutters=%u worst_frame_ms=%llu worst_io_ms=%llu",
        result_label ? result_label : "UNKNOWN",
        g_diag_total_frames,
        g_diag_stutter_frames,
        (unsigned long long)g_diag_worst_frame_ms,
        (unsigned long long)g_diag_worst_io_ms
    );
    if (g_diag_bound && g_diag_log_path[0] != '\0')
        usb_ok = diag_write_snapshot_to(g_diag_log_path, result_label ? result_label : "final");
    if (!usb_ok) {
        snprintf(mc_dir, sizeof(mc_dir), "mc%d:/TITAN", g_diag_memory_card);
        (void)mkdir(mc_dir, 0777);
        mc_ok = diag_write_snapshot_to(g_diag_mc_path, result_label ? result_label : "final");
    }
    return usb_ok || mc_ok;
}
