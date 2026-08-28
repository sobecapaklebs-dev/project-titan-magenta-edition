// update_check.cpp — see update_check.h.
#include "update_check.h"

#include "app_config.h"
#include "app_version.h"
#include "arg_triage.h"
#include "config_schema.h"
#include "update_check_core.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// The canonical endpoint. GE007_UPDATE_CHECK_URL overrides it for testing (a
// file:// path or a local mock server) so the whole banner path can be exercised
// with zero real network. curl handles file:// natively.
const char *kDefaultUrl =
    "https://api.github.com/repos/akratch/mgb64/releases/latest";

// GitHub's JSON is small; hard-cap the read so a hostile/huge body can't grow
// memory. 64KB is comfortably above a real /releases/latest payload.
const size_t kMaxResponse = 64 * 1024;

SDL_atomic_t s_started;    // 1 once the worker has been (or was declined to be) launched
SDL_atomic_t s_done;       // 1 when the worker has finished
SDL_atomic_t s_hasUpdate;  // 1 when s_tag holds a strictly-newer release tag
SDL_atomic_t s_checked;    // 1 when a real network response was parsed + compared
SDL_atomic_t s_quiesced;   // 1 once the app is shutting down: worker stops logging
char         s_tag[128];   // written by the worker before publishing s_hasUpdate

// M1: the worker is detached, so if the user quits within curl's 3s window it
// can outlive host.shutdown()/DiagLog teardown. Its data targets are all
// process-lifetime statics (safe), but a late fprintf(stderr) would race the
// closing DiagLog pipe — so the worker's log lines are gated on s_quiesced
// (set by UpdateCheck_quiesce before shutdown).
bool logOk() { return SDL_AtomicGet(&s_quiesced) == 0; }

const char *resolveUrl() {
    const char *env = std::getenv("GE007_UPDATE_CHECK_URL");
    return (env && env[0]) ? env : kDefaultUrl;
}

// Run `curl -fsS --max-time 3 <url>` with NO shell and NO visible console
// window, capturing up to `cap-1` bytes of stdout into `buf` (NUL-terminated).
// Returns bytes read; 0 on any failure (curl missing, non-zero exit, timeout,
// empty body). Platform-specific below.
size_t runCurl(const char *url, char *buf, size_t cap);

int worker(void * /*arg*/) {
    static char resp[kMaxResponse];
    size_t n = runCurl(resolveUrl(), resp, sizeof(resp));
    if (n > 0) {
        char tag[128];
        if (mgb_update_extract_tag(resp, n, tag, sizeof(tag))) {
            SDL_AtomicSet(&s_checked, 1);  // a real, comparable response arrived
            if (mgb_update_remote_is_newer(tag, AppVersion())) {
                std::snprintf(s_tag, sizeof(s_tag), "%s", tag);
                SDL_AtomicSet(&s_hasUpdate, 1);
                if (logOk())
                    std::fprintf(stderr, "[update] newer release available: %s (current %s)\n",
                                 tag, AppVersion());
            } else if (logOk()) {
                std::fprintf(stderr, "[update] up to date (current %s)\n", AppVersion());
            }
        } else if (logOk()) {
            std::fprintf(stderr, "[update] response unparseable; no banner\n");
        }
    } else if (logOk()) {
        std::fprintf(stderr, "[update] no response (offline / curl unavailable); no banner\n");
    }
    SDL_AtomicSet(&s_done, 1);
    return 0;
}

}  // namespace

// ---- subprocess: POSIX (posix_spawnp — no shell, so the URL cannot inject) ----
#if !defined(_WIN32)
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace {
size_t runCurl(const char *url, char *buf, size_t cap) {
    if (!url || !url[0] || cap == 0) return 0;

    int pfd[2];
    if (pipe(pfd) != 0) return 0;

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pfd[1], STDOUT_FILENO);  // stdout -> pipe
    // curl's -S diagnostics go to a hushed stderr so an offline launch is silent.
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&fa, pfd[0]);
    posix_spawn_file_actions_addclose(&fa, pfd[1]);

    char *argv[] = {(char *)"curl", (char *)"-fsS", (char *)"--max-time",
                    (char *)"3",    (char *)url,     nullptr};
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, "curl", &fa, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(pfd[1]);
    if (rc != 0) {  // curl not found on PATH, etc.
        close(pfd[0]);
        return 0;
    }

    size_t total = 0;
    for (;;) {
        if (total >= cap - 1) break;
        ssize_t r = read(pfd[0], buf + total, cap - 1 - total);
        if (r <= 0) break;
        total += (size_t)r;
    }
    buf[total] = '\0';
    close(pfd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    // On a non-zero curl exit (-f on HTTP >=400, timeout, exec failure) there is
    // no usable body; treat any leftover partial as nothing.
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) return 0;
    if (!WIFEXITED(status)) return 0;
    return total;
}
}  // namespace

// ---- subprocess: Windows (CreateProcess + CREATE_NO_WINDOW — no console flash) ----
#else
#include <vector>
#include <windows.h>

namespace {
size_t runCurl(const char *url, char *buf, size_t cap) {
    if (!url || !url[0] || cap == 0) return 0;
    // The command line is quoted with '"'; reject a URL that carries one so it
    // can't break the argument (posix_spawnp needs no such guard). A trailing
    // '\' would escape our closing quote under CommandLineToArgv rules — reject
    // that too (L1; no legit URL ends in a backslash).
    if (std::strchr(url, '"') || url[std::strlen(url) - 1] == '\\') return 0;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);  // our read end stays private

    // NUL for the child's stdin + stderr (keeps curl's -S diagnostics silent and
    // gives it a valid stdin under the -mwindows GUI subsystem).
    HANDLE nul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, nullptr);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = wr;
    si.hStdError = nul;

    std::string cmd = "curl.exe -fsS --max-time 3 \"";
    cmd += url;
    cmd += "\"";
    std::vector<char> cl(cmd.begin(), cmd.end());
    cl.push_back('\0');

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    // CREATE_NO_WINDOW: run curl.exe with no console window so nothing flashes
    // on screen (the app is a -mwindows GUI process). curl is spawned as a
    // subprocess, never linked — the import table stays clean.
    BOOL ok = CreateProcessA(nullptr, cl.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {  // curl.exe not found, etc.
        CloseHandle(rd);
        return 0;
    }
    CloseHandle(pi.hThread);

    size_t total = 0;
    for (;;) {
        if (total >= cap - 1) break;
        DWORD r = 0;
        if (!ReadFile(rd, buf + total, (DWORD)(cap - 1 - total), &r, nullptr) || r == 0) break;
        total += r;
    }
    buf[total] = '\0';
    CloseHandle(rd);

    WaitForSingleObject(pi.hProcess, 4000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (code != 0) return 0;  // curl failed (-f / timeout): no usable body
    return total;
}
}  // namespace
#endif

// ---- public API --------------------------------------------------------------

void UpdateCheck_start(int argc, char **argv) {
    if (SDL_AtomicGet(&s_started)) return;  // at most once per session

    // Automation/deterministic invocations: zero network in test lanes. This is
    // the same gate the interactive path already sits behind (main_app.cpp routes
    // automation to the headless engine before the launcher), re-checked here so
    // the promise holds regardless of call site.
    if (mgb_is_automation_invocation(argc, argv)) {
        SDL_AtomicSet(&s_started, 1);
        SDL_AtomicSet(&s_done, 1);
        std::fprintf(stderr, "[update] skipped: automation invocation\n");
        return;
    }
    // Env opt-out (house style) — honored even if the ini says on.
    const char *env = std::getenv("GE007_UPDATE_CHECK");
    if (env && std::strcmp(env, "0") == 0) {
        SDL_AtomicSet(&s_started, 1);
        SDL_AtomicSet(&s_done, 1);
        std::fprintf(stderr, "[update] skipped: GE007_UPDATE_CHECK=0\n");
        return;
    }
    // Setting toggle (Game.CheckForUpdates, default on).
    if (mgb_config_get_int("Game.CheckForUpdates", 1) == 0) {
        SDL_AtomicSet(&s_started, 1);
        SDL_AtomicSet(&s_done, 1);
        std::fprintf(stderr, "[update] skipped: Game.CheckForUpdates=0\n");
        return;
    }

    SDL_AtomicSet(&s_started, 1);
    SDL_Thread *t = SDL_CreateThread(worker, "mgb64-update-check", nullptr);
    if (t) {
        SDL_DetachThread(t);
    } else {
        // [AUDIT-0044] mark the check finished so the UI leaves "Checking..." and
        // falls through to the offline/unavailable state instead of hanging.
        SDL_AtomicSet(&s_done, 1);
        std::fprintf(stderr, "[update] SDL_CreateThread failed: %s\n", SDL_GetError());
    }
}

int UpdateCheck_bannerTag(char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    if (!SDL_AtomicGet(&s_done) || !SDL_AtomicGet(&s_hasUpdate)) return 0;
    if (AppConfig::get("update.dismissed_tag") == std::string(s_tag)) return 0;
    std::snprintf(out, cap, "%s", s_tag);
    return 1;
}

int UpdateCheck_isDone(void) { return SDL_AtomicGet(&s_done); }

int UpdateCheck_didCheck(void) { return SDL_AtomicGet(&s_checked); }

void UpdateCheck_quiesce(void) { SDL_AtomicSet(&s_quiesced, 1); }

void UpdateCheck_dismiss(const char *tag) {
    if (!tag) return;
    // Belt-and-suspenders vs H1: the tag already passed the parse-side charset
    // allow-list, but the ini is newline-delimited key=value, so strip any
    // control byte here too in case another tag source ever feeds this path.
    char clean[128];
    size_t o = 0;
    for (const char *p = tag; *p && o + 1 < sizeof(clean); ++p) {
        if ((unsigned char)*p >= 0x20) clean[o++] = *p;
    }
    clean[o] = '\0';
    AppConfig::set("update.dismissed_tag", clean);
    AppConfig::save();
}
