// rom_scan.cpp — see rom_scan.h.
#include "rom_scan.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <sys/stat.h>
#if defined(_WIN32)
#  include <strings.h>  // MinGW provides strcasecmp
#endif

namespace romscan {

namespace {

bool caseLess(const std::string &a, const std::string &b) {
    return strcasecmp(a.c_str(), b.c_str()) < 0;
}

// stat-based directory test — reliable where dirent d_type is DT_UNKNOWN.
bool pathIsDir(const std::string &path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return (st.st_mode & S_IFMT) == S_IFDIR;
}

std::string joinPath(const std::string &dir, const char *name) {
    if (dir.empty()) return name;
    char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') return dir + name;
    return dir + "/" + name;
}

// Parent of `dir`, or empty if it has none (root or bare name).
std::string parentOf(const std::string &dir) {
    if (dir.empty()) return "";
    std::string d = dir;
    // Drop a trailing slash (but keep a lone "/" as root).
    while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) d.pop_back();
    size_t slash = d.find_last_of("/\\");
    if (slash == std::string::npos) return "";     // bare name, no parent path
    if (slash == 0) return "/";                     // parent is filesystem root
    return d.substr(0, slash);
}

}  // namespace

bool hasRomExtension(const char *name) {
    if (!name) return false;
    size_t len = std::strlen(name);
    if (len < 5) return false;  // "x.z64"
    const char *ext = name + len - 4;
    return strcasecmp(ext, ".z64") == 0 ||
           strcasecmp(ext, ".n64") == 0 ||
           strcasecmp(ext, ".v64") == 0;
}

bool listDir(const std::string &dir, std::vector<Entry> &out, bool includeParent) {
    out.clear();
    DIR *d = opendir(dir.c_str());
    if (!d) return false;

    std::vector<Entry> dirs, files;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr) {
        const char *nm = e->d_name;
        if (nm[0] == '.') continue;  // hidden, ".", ".." (parent handled below)
        std::string full = joinPath(dir, nm);
        if (pathIsDir(full)) {
            dirs.push_back({nm, full, true});
        } else if (hasRomExtension(nm)) {
            files.push_back({nm, full, false});
        }
    }
    closedir(d);

    std::sort(dirs.begin(), dirs.end(),
              [](const Entry &a, const Entry &b) { return caseLess(a.name, b.name); });
    std::sort(files.begin(), files.end(),
              [](const Entry &a, const Entry &b) { return caseLess(a.name, b.name); });

    if (includeParent) {
        std::string parent = parentOf(dir);
        if (!parent.empty()) out.push_back({"..", parent, true});
    }
    out.insert(out.end(), dirs.begin(), dirs.end());
    out.insert(out.end(), files.begin(), files.end());
    return true;
}

std::vector<std::string> defaultScanDirs(const char *homeOverride) {
    std::vector<std::string> dirs;
    dirs.push_back(".");  // cwd (next-to-executable / launch dir)

    const char *home = (homeOverride && homeOverride[0]) ? homeOverride : std::getenv("HOME");
#if defined(_WIN32)
    if (!home || !home[0]) home = std::getenv("USERPROFILE");
#endif
    if (home && home[0]) {
        std::string h = home;
        dirs.push_back(joinPath(h, "Downloads"));
        dirs.push_back(joinPath(h, "Documents"));
        dirs.push_back(joinPath(h, "Desktop"));
        dirs.push_back(h);
    }
    return dirs;
}

std::vector<std::string> scanDirsForRoms(const std::vector<std::string> &dirs, size_t maxResults) {
    std::vector<std::string> found;
    for (const std::string &dir : dirs) {
        if (found.size() >= maxResults) break;
        DIR *d = opendir(dir.c_str());
        if (!d) continue;
        std::vector<std::string> here;
        struct dirent *e;
        while ((e = readdir(d)) != nullptr) {
            if (e->d_name[0] == '.') continue;
            if (!hasRomExtension(e->d_name)) continue;
            std::string full = joinPath(dir, e->d_name);
            if (pathIsDir(full)) continue;  // a dir named "foo.z64" is not a ROM
            here.push_back(std::move(full));
        }
        closedir(d);
        std::sort(here.begin(), here.end(), caseLess);
        for (std::string &p : here) {
            if (found.size() >= maxResults) break;
            if (std::find(found.begin(), found.end(), p) == found.end())
                found.push_back(std::move(p));
        }
    }
    return found;
}

}  // namespace romscan
