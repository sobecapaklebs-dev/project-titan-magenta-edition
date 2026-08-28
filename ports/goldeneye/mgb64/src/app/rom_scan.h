// rom_scan.h — keyboard-free ROM discovery for the app shell (RX.4).
//
// The native file dialog (NFD) is not controller-navigable, so on a handheld the
// first-run "pick your ROM" step is a hard stop. This module provides the
// pad/touch path: (1) an auto-scan of the same common locations the CLI already
// checks, presented as a pick-list, and (2) a plain directory browser the ImGui
// panel renders as pad-navigable Selectable rows.
//
// Deliberately SDL-/ImGui-free (only the C++ stdlib + POSIX/MinGW dirent) so it
// links into a tiny unit test. Discovery is EXTENSION-ONLY (.z64/.n64/.v64); the
// caller still runs mgb_validate_rom on the chosen file (ui_rom.cpp), so the real
// GoldenEye header check is unchanged.
#ifndef MGB64_ROM_SCAN_H
#define MGB64_ROM_SCAN_H

#include <string>
#include <vector>

namespace romscan {

// A filesystem entry for the in-app browser.
struct Entry {
    std::string name;   // display basename ("Downloads", "rom.z64", "..")
    std::string path;   // full path to navigate to / select
    bool        isDir;
};

// True when `name` ends with a ROM extension (.z64/.n64/.v64), case-insensitive.
bool hasRomExtension(const char *name);

// List `dir`'s sub-directories + ROM files, sorted (dirs first, then files, each
// case-insensitive). Hidden entries (leading '.') are skipped. When
// includeParent is true and `dir` has a parent, a ".." entry is prepended.
// Returns false (out cleared) if `dir` can't be opened.
bool listDir(const std::string &dir, std::vector<Entry> &out, bool includeParent = true);

// Default scan directories, in the CLI's order: cwd, $HOME/Downloads,
// $HOME/Documents, $HOME/Desktop, $HOME. `homeOverride` replaces $HOME when
// non-null and non-empty (used by tests for determinism).
std::vector<std::string> defaultScanDirs(const char *homeOverride = nullptr);

// Scan `dirs` for ROM files by extension; return full paths in scan order,
// de-duplicated, capped at maxResults.
std::vector<std::string> scanDirsForRoms(const std::vector<std::string> &dirs,
                                         size_t maxResults = 64);

}  // namespace romscan

#endif  // MGB64_ROM_SCAN_H
