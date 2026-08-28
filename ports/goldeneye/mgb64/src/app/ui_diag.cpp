// ui_diag.cpp — Diagnostics: live log console, export bundle, report-a-bug.
#include "ui_launcher.h"
#include "app_theme.h"
#include "app_version.h"
#include "diag_log.h"
#include "ui_common.h"
#include "config_schema.h"   // mgb_config_path (AUDIT-0047)

#include "imgui.h"
#include <SDL.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <glad/glad.h>
#endif

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

const char *kIssueUrl = "https://github.com/akratch/mgb64/issues/new?template=bug_report.md";

std::string prefsDir() {
    char *p = SDL_GetPrefPath("MGB64", "MGB64");
    std::string d = p ? p : "";
    if (p) SDL_free(p);
    return d;
}

// Replace non-printable / non-ASCII bytes (except \n \t) so the Roboto atlas
// never renders tofu for log lines carrying arrows/status symbols.
void sanitizeAscii(char *buf) {
    for (char *p = buf; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '\n' || c == '\t') continue;
        if (c < 32 || c > 126) *p = '?';
    }
}

void writeSysinfo(const std::string &path, const LauncherState &s) {
    std::ofstream f(path);
    if (!f) return;
    f << "MGB64 diagnostics\n";
    f << "app: v" << AppVersion() << " (GL)\n";
    f << "platform: " << SDL_GetPlatform() << "\n";
    f << "cpu-count: " << SDL_GetCPUCount() << "\n";
    f << "ram-mb: " << SDL_GetSystemRAM() << "\n";
    SDL_version v;
    SDL_GetVersion(&v);
    f << "sdl: " << (int)v.major << "." << (int)v.minor << "." << (int)v.patch << "\n";
    const char *rend = (const char *)glGetString(GL_RENDERER);
    const char *glver = (const char *)glGetString(GL_VERSION);
    f << "gl-renderer: " << (rend ? rend : "?") << "\n";
    f << "gl-version: " << (glver ? glver : "?") << "\n";
    // ROM: metadata ONLY (never bytes); path redacted to filename. Scan for BOTH
    // separators so a Windows "C:\Users\<name>\rom.z64" path is not leaked whole
    // (strrchr('/') alone left the full drive path, incl. the account name)
    // [AUDIT-0048].
    if (s.romPath[0]) {
        const char *fn = s.romPath;
        for (const char *p = s.romPath; *p; ++p)
            if (*p == '/' || *p == '\\') fn = p + 1;
        f << "rom-file: " << fn << "\n";
        f << "rom-region: " << s.romInfo.region << "\n";
        f << "rom-byteorder: " << s.romInfo.byte_order << "\n";
        f << "rom-size-mb: " << (s.romInfo.size_bytes / (1024.0 * 1024.0)) << "\n";
        f << "rom-valid: " << (s.romInfo.valid ? "yes" : "no") << "\n";
    } else {
        f << "rom: none selected\n";
    }
}

std::string exportDiagnostics(const LauncherState &s) {
    namespace fs = std::filesystem;
    std::string base = prefsDir();
    fs::path out = fs::path(base) / "mgb64-diagnostics";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out, ec);

    auto copyIfExists = [&](const fs::path &src, const char *name) {
        if (fs::exists(src, ec))
            fs::copy_file(src, out / name, fs::copy_options::overwrite_existing, ec);
    };
    std::string logp = DiagLog_path();
    if (!logp.empty()) copyIfExists(logp, "mgb64.log");
    copyIfExists(fs::path(base) / "mgb64.prev.log", "mgb64.prev.log");
    // AUDIT-0047: export the engine config from its RESOLVED save-directory path,
    // not a guessed CWD-relative name (a packaged .app has CWD=/, and --savedir
    // moves it elsewhere), so the real ge007.ini is actually included.
    {
        const char *cfg = mgb_config_path();
        if (cfg && cfg[0]) copyIfExists(fs::path(cfg), "ge007.ini");
    }
    copyIfExists(fs::path(base) / "mgb64_app.ini", "mgb64_app.ini");
    writeSysinfo((out / "sysinfo.txt").string(), s);
    return out.string();
}

}  // namespace

void DiagPanel_draw(LauncherState &s, LauncherAction & /*out*/) {
    ui::SectionHeader("Diagnostics", "Live log, plus a one-click export to attach to a bug report.");

    if (ui::PrimaryButton("Export Diagnostics", ImVec2(200, 40))) {
        std::string dir = exportDiagnostics(s);
        std::string url = "file://" + dir;
        SDL_OpenURL(url.c_str());  // reveal the folder
    }
    ImGui::SameLine();
    if (ImGui::Button("Report a Bug", ImVec2(160, 40))) SDL_OpenURL(kIssueUrl);
    ui::Gap(ui::kGapXS);
    ui::TextSubtle("Export writes mgb64.log + ge007.ini + sysinfo to a folder (no ROM data) and opens it.");
    ui::Gap(ui::kGapM);

    ImGui::TextUnformatted("Log");
    ui::Gap(ui::kGapXS);
    static char buf[64 * 1024];
    static int prevN = 0;
    int n = DiagLog_snapshot(buf, sizeof(buf));
    sanitizeAscii(buf);
    bool grew = (n != prevN);
    prevN = n;

    ImGui::BeginChild("##log", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushFont(AppTheme::fonts().small);
    ImGui::TextUnformatted(buf, buf + n);
    ImGui::PopFont();
    if (grew) ImGui::SetScrollHereY(1.0f);  // stick to the latest when new lines arrive
    ImGui::EndChild();
}
