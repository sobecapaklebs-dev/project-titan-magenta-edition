// app_version.h — the single compiled-in product version for the app shell.
//
// The value comes from CMake (-DMGB64_APP_VERSION_STR="${MGB64_VER_STR}", derived
// from MGB64_VERSION). Release CI threads the real version through; a plain dev
// build gets "0.0.0-dev", which the update check treats as "never newer" so dev
// builds never show the banner. Used by the launcher's version label and by the
// GitHub-release comparison in update_check.cpp.
#ifndef MGB64_APP_VERSION_H
#define MGB64_APP_VERSION_H

#ifndef MGB64_APP_VERSION_STR
#define MGB64_APP_VERSION_STR "0.0.0-dev"
#endif

inline const char *AppVersion() { return MGB64_APP_VERSION_STR; }

#endif  // MGB64_APP_VERSION_H
