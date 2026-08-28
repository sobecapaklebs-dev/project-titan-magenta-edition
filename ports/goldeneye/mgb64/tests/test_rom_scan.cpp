// test_rom_scan.cpp — unit test for the keyboard-free ROM discovery (RX.4).
#include "rom_scan.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>  // mkdtemp, rmdir

using romscan::Entry;

static void touch(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (f) { std::fputc('x', f); std::fclose(f); }
}

static bool listHas(const std::vector<Entry> &v, const char *name, bool isDir) {
    for (const Entry &e : v)
        if (e.name == name && e.isDir == isDir) return true;
    return false;
}

static bool vecHasSuffix(const std::vector<std::string> &v, const char *suffix) {
    for (const std::string &s : v)
        if (s.size() >= std::strlen(suffix) &&
            s.compare(s.size() - std::strlen(suffix), std::strlen(suffix), suffix) == 0)
            return true;
    return false;
}

int main() {
    int fails = 0;
    auto expect = [&](const char *name, bool cond) {
        if (!cond) { std::printf("FAIL: %s\n", name); ++fails; }
    };

    // --- hasRomExtension ---
    expect("z64 accepted", romscan::hasRomExtension("game.z64"));
    expect("N64 accepted (case-insensitive)", romscan::hasRomExtension("GAME.N64"));
    expect("v64 accepted", romscan::hasRomExtension("a.v64"));
    expect("txt rejected", !romscan::hasRomExtension("readme.txt"));
    expect("no-ext rejected", !romscan::hasRomExtension("z64"));
    expect("null rejected", !romscan::hasRomExtension(nullptr));

    // --- scratch tree: <base>/scan/{a.z64, notes.txt, sub/} ---
    char tmpl[] = "romscan_test_XXXXXX";
    const char *base = mkdtemp(tmpl);
    if (!base) { std::printf("FAIL: mkdtemp\n"); return 1; }
    std::string root = base;
    std::string scan = root + "/scan";
    std::string sub  = scan + "/sub";
    mkdir(scan.c_str(), 0777);
    mkdir(sub.c_str(), 0777);
    touch(scan + "/a.z64");
    touch(scan + "/notes.txt");     // ignored (not a ROM)
    touch(sub + "/deep.n64");       // not found by a shallow scan of `scan`
    std::string romNamedDir = scan + "/bundle.z64";  // a DIRECTORY named like a ROM
    mkdir(romNamedDir.c_str(), 0777);

    // --- scanDirsForRoms finds the fake .z64 by extension, ignores non-ROMs ---
    std::vector<std::string> found = romscan::scanDirsForRoms({scan});
    expect("scan finds a.z64", vecHasSuffix(found, "a.z64"));
    expect("scan ignores notes.txt", !vecHasSuffix(found, "notes.txt"));
    expect("scan is shallow (no sub/deep.n64)", !vecHasSuffix(found, "deep.n64"));
    expect("scan skips a directory named bundle.z64", !vecHasSuffix(found, "bundle.z64"));
    expect("scan of empty dir list is empty", romscan::scanDirsForRoms({}).empty());

    // --- listDir: dirs first, then ROM files; parent (..) prepended ---
    std::vector<Entry> entries;
    expect("listDir opens", romscan::listDir(scan, entries, /*includeParent=*/true));
    expect("listDir shows sub/", listHas(entries, "sub", true));
    expect("listDir shows a.z64", listHas(entries, "a.z64", false));
    expect("listDir hides notes.txt", !listHas(entries, "notes.txt", false));
    expect("listDir has parent ..", listHas(entries, "..", true));
    // Ordering: ".." first, then dirs, then files.
    if (entries.size() >= 3) {
        expect("first entry is ..", entries.front().name == "..");
        // last entry should be the ROM file (files sort after dirs)
        expect("last entry is a.z64", entries.back().name == "a.z64");
    } else {
        std::printf("FAIL: listDir entry count %zu\n", entries.size());
        ++fails;
    }

    // --- listDir on a bad path fails and clears out ---
    std::vector<Entry> none;
    expect("listDir bad path returns false", !romscan::listDir(root + "/nope", none));
    expect("listDir bad path clears out", none.empty());

    // --- defaultScanDirs honors homeOverride: includes <home>/Downloads etc. ---
    std::vector<std::string> dirs = romscan::defaultScanDirs(root.c_str());
    expect("defaultScanDirs includes cwd", std::find(dirs.begin(), dirs.end(), ".") != dirs.end());
    expect("defaultScanDirs includes home/Downloads", vecHasSuffix(dirs, "/Downloads"));
    expect("defaultScanDirs includes home root", std::find(dirs.begin(), dirs.end(), root) != dirs.end());

    // --- end-to-end: place a ROM in <home>/Downloads, discover via defaults ---
    std::string dl = root + "/Downloads";
    mkdir(dl.c_str(), 0777);
    touch(dl + "/ge.z64");
    std::vector<std::string> autolist = romscan::scanDirsForRoms(romscan::defaultScanDirs(root.c_str()));
    expect("auto-scan finds home/Downloads/ge.z64", vecHasSuffix(autolist, "ge.z64"));

    // cleanup (best-effort)
    std::remove((scan + "/a.z64").c_str());
    std::remove((scan + "/notes.txt").c_str());
    std::remove((sub + "/deep.n64").c_str());
    std::remove((dl + "/ge.z64").c_str());
    rmdir(romNamedDir.c_str());
    rmdir(sub.c_str()); rmdir(scan.c_str()); rmdir(dl.c_str()); rmdir(root.c_str());

    if (fails == 0) { std::printf("PASS: all rom_scan cases\n"); return 0; }
    std::printf("%d failure(s)\n", fails);
    return 1;
}
