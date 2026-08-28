// ui_rom.cpp — the Game ROM panel: pickers, validation card, Play.
//
// Three ways to select a ROM, so a controller/touch handheld is never stuck at a
// native dialog it can't drive (RX.4):
//   1. Detected ROMs  — auto-scan of the common locations the CLI checks, shown
//                        as a pad-navigable pick-list.
//   2. Browse in app  — a plain directory browser (Selectable rows, pad-nav).
//   3. Choose ROM...  — the native NFD dialog (mouse/desktop convenience).
// Plus drag-and-drop (SDL_DROPFILE) routed in from the app host. Whatever the
// path, the file still flows through mgb_validate_rom + the validation card.
#include "ui_launcher.h"
#include "app_config.h"
#include "app_theme.h"
#include "rom_scan.h"
#include "ui_common.h"

#include "imgui.h"
#include "nfd.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// In-app browser + auto-scan state. The launcher is a single instance, so file
// statics keep this local without growing the shared LauncherState.
bool                        g_browserOpen = false;
std::string                 g_browserDir;
std::vector<romscan::Entry> g_browserEntries;
bool                        g_scanDone = false;
std::vector<std::string>    g_found;

const char *baseName(const char *path) {
    const char *s = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') s = p + 1;
    }
    return s;
}

// Directory portion of `path` (into `buf`), or "." when there is none.
const char *dirName(const char *path, std::string &buf) {
    const char *b = baseName(path);
    if (b == path) { buf = "."; return buf.c_str(); }
    buf.assign(path, (size_t)(b - path));
    while (buf.size() > 1 && (buf.back() == '/' || buf.back() == '\\')) buf.pop_back();
    return buf.c_str();
}

void setRom(LauncherState &st, const char *path) {
    if (!path) return;
    std::snprintf(st.romPath, sizeof(st.romPath), "%s", path);
    st.romInfo = mgb_validate_rom(st.romPath);
    // Only remember a ROM that validated — never persist a garbage path from a
    // dropped/browsed file that failed the header check.
    if (st.romInfo.valid) {
        AppConfig::set("last_rom", st.romPath);
        AppConfig::save();
    }
}

void pickRom(LauncherState &st) {
    if (NFD_Init() != NFD_OKAY) return;
    nfdu8char_t *out = nullptr;
    nfdu8filteritem_t filters[1] = {{"Nintendo 64 ROM", "z64,n64,v64"}};
    nfdresult_t r = NFD_OpenDialogU8(&out, filters, 1, nullptr);
    if (r == NFD_OKAY && out) {
        setRom(st, out);
        NFD_FreePathU8(out);
    }
    NFD_Quit();
}

void ensureScan() {
    if (g_scanDone) return;
    g_scanDone = true;
    g_found = romscan::scanDirsForRoms(romscan::defaultScanDirs());
}

void navigateBrowser(const std::string &dir) {
    g_browserDir = dir;
    if (!romscan::listDir(g_browserDir, g_browserEntries, /*includeParent=*/true)) {
        g_browserEntries.clear();
    }
}

// Seed the browser at the current ROM's folder, else the first detected ROM's
// folder, else $HOME, else cwd — so it opens somewhere useful.
void openBrowser(const LauncherState &st) {
    std::string start, tmp;
    const char *home = std::getenv("HOME");
    if (st.romPath[0]) {
        start = dirName(st.romPath, tmp);
    } else if (!g_found.empty()) {
        start = dirName(g_found.front().c_str(), tmp);
    } else if (home && home[0]) {
        start = home;
    } else {
        start = ".";
    }
    navigateBrowser(start);
    g_browserOpen = true;
}

}  // namespace

// Public entry so the app host's drag-and-drop (SDL_DROPFILE) can set the ROM.
void RomPanel_setRom(LauncherState &st, const char *path) { setRom(st, path); }

void RomPanel_ensureInit(LauncherState &st) {
    if (st.romInitialized) return;
    st.romInitialized = true;
    AppConfig::load();
    std::string last = AppConfig::get("last_rom");
    if (!last.empty()) setRom(st, last.c_str());
}

void RomPanel_draw(LauncherState &st, LauncherAction &out) {
    ui::SectionHeader("Game ROM",
                      "Provide your own legally-dumped GoldenEye 007 cartridge (.z64 / .n64 / .v64).");
    ensureScan();

    // First-run welcome: with no remembered ROM, the very first launch lands here.
    // Greet the player and set expectations before the picker (vanishes once a ROM
    // is chosen, so it never nags returning users).
    if (!st.romPath[0]) {
        ImGui::PushFont(AppTheme::fonts().title);
        ImGui::PushStyleColor(ImGuiCol_Text, AppTheme::accent());
        ImGui::TextUnformatted("Welcome to MGB64");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ui::Gap(ui::kGapXS);
        ImGui::TextWrapped(
            "A faithful native port of GoldenEye 007. It ships no game data \xE2\x80\x94 bring "
            "your own legally-dumped cartridge and MGB64 remembers it for next time. Use any "
            "option below: pick a detected ROM, browse in app, choose a file, or drag one "
            "onto the window.");
        ui::Gap(ui::kGapM);
    }

    // Picker actions. Native dialog for mouse/desktop; the in-app browser is the
    // controller/touch path (NFD can't be driven by a gamepad).
    if (ImGui::Button(st.romPath[0] ? "Change ROM..." : "Choose ROM...", ui::kBtnSecondary())) {
        pickRom(st);
    }
    ImGui::SameLine();
    if (ImGui::Button(g_browserOpen ? "Close browser" : "Browse in app...", ui::kBtnSecondary())) {
        if (g_browserOpen) g_browserOpen = false; else openBrowser(st);
    }
    ui::TextSubtle("Or drag a ROM file onto the window.");
    ui::Gap(ui::kGapS);

    // Auto-detected ROMs pick-list (pad-navigable). Hidden while browsing.
    if (!g_browserOpen && !g_found.empty()) {
        ImGui::PushFont(AppTheme::fonts().small);
        ui::TextSubtle("Detected ROMs");
        ImGui::PopFont();
        ui::CardBegin("##romfound", AppTheme::primary(), 96.0f * AppTheme::uiScale());
        std::string tmp;
        for (const std::string &p : g_found) {
            ImGui::PushID(p.c_str());
            bool selected = (std::strcmp(p.c_str(), st.romPath) == 0);
            if (ImGui::Selectable(baseName(p.c_str()), selected)) setRom(st, p.c_str());
            ImGui::SameLine();
            ImGui::PushFont(AppTheme::fonts().small);
            ui::TextSubtle("  %s", dirName(p.c_str(), tmp));
            ImGui::PopFont();
            ImGui::PopID();
        }
        ui::CardEnd();
        ui::Gap(ui::kGapS);
    }

    // In-app directory browser.
    if (g_browserOpen) {
        ImGui::PushFont(AppTheme::fonts().small);
        ui::TextSubtle("%s", g_browserDir.c_str());
        ImGui::PopFont();
        ui::CardBegin("##rombrowser", AppTheme::primary(), 220.0f * AppTheme::uiScale());
        if (g_browserEntries.empty()) ui::TextSubtle("(empty or unreadable)");
        std::string nextDir;  // deferred navigation (don't relist mid-loop)
        bool doNavigate = false;
        for (const romscan::Entry &e : g_browserEntries) {
            ImGui::PushID(e.path.c_str());
            const char *icon = e.isDir ? (e.name == ".." ? "\xE2\x86\xB0 " : "\xF0\x9F\x93\x81 ")
                                       : "\xF0\x9F\x8E\xAE ";
            char row[1200];
            std::snprintf(row, sizeof(row), "%s%s", icon, e.name.c_str());
            if (ImGui::Selectable(row)) {
                if (e.isDir) { nextDir = e.path; doNavigate = true; }
                else { setRom(st, e.path.c_str()); g_browserOpen = false; }
            }
            ImGui::PopID();
        }
        ui::CardEnd();
        if (doNavigate) navigateBrowser(nextDir);
        ui::Gap(ui::kGapS);
    }

    if (st.romPath[0]) {
        const bool ok = st.romInfo.valid != 0;
        const ImVec4 mark = ok ? AppTheme::good() : AppTheme::bad();
        ui::CardBegin("##romcard", mark, 140.0f * AppTheme::uiScale());
        ImGui::PushFont(AppTheme::fonts().title);
        ImGui::TextUnformatted(baseName(st.romPath));
        ImGui::PopFont();
        ui::Gap(ui::kGapXS);
        // Lead with a status WORD so validity reads without relying on the card
        // color alone (colorblind accessibility). Em-dash is already font-safe here.
        ImGui::PushStyleColor(ImGuiCol_Text, mark);
        ImGui::TextWrapped("%s \xE2\x80\x94 %s", ok ? "Ready" : "Problem", st.romInfo.message);
        ImGui::PopStyleColor();
        ui::Gap(ui::kGapXS);
        ImGui::PushFont(AppTheme::fonts().small);
        ui::TextSubtle("Region %s   \xE2\x80\xA2   %s   \xE2\x80\xA2   %.1f MB", st.romInfo.region,
                       st.romInfo.byte_order, st.romInfo.size_bytes / (1024.0 * 1024.0));
        ui::TextSubtle("%s", st.romPath);
        ImGui::PopFont();
        ui::CardEnd();
    } else {
        // Same-height muted card so the layout doesn't jump before selection.
        ui::CardBegin("##romempty", AppTheme::subtle(), 140.0f * AppTheme::uiScale());
        ui::Gap(ui::kGapM);
        ImGui::PushFont(AppTheme::fonts().title);
        ui::TextSubtle("No ROM selected");
        ImGui::PopFont();
        ui::Gap(ui::kGapXS);
        ui::TextSubtle("Pick a detected ROM above, browse in app, or click \"Choose ROM...\".");
        ui::CardEnd();
    }

    ui::Gap(ui::kGapM);
    PlayButton_draw(st, out);
}
