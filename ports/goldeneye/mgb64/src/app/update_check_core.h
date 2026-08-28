// update_check_core.h — pure, SDL-free, network-free helpers for the update
// check. Split out from update_check.cpp so the tag-parse + semver-compare logic
// can be unit-tested (tests/test_update_check.cpp) without linking SDL or curl.
#ifndef MGB64_UPDATE_CHECK_CORE_H
#define MGB64_UPDATE_CHECK_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Extract the "tag_name" string value from a GitHub /releases/latest JSON body.
// Scans for the key anywhere in `json` (robust to field order), then reads the
// quoted value decoding the common JSON escapes (\" \\ \/ \n \t \uXXXX-as-'?').
// `len` bounds the scan (the caller hard-caps the HTTP read at 64KB). Writes a
// NUL-terminated tag into out[cap]. Returns 1 if a non-empty tag was found.
//
// SECURITY: the DECODED tag is restricted to the release-tag charset
// [A-Za-z0-9._+-]; any other decoded byte (control chars, '/', '"', spaces,
// ...) rejects the WHOLE tag (returns 0, out=""). The tag is persisted into the
// newline-delimited app ini on Dismiss, so a decoded '\n' would otherwise be a
// config-key injection primitive; real release tags always fit the set.
int mgb_update_extract_tag(const char *json, size_t len, char *out, size_t cap);

// True if `version` should be treated as a dev/unparseable build — i.e. one that
// must NEVER show the update banner. That is: empty, no parseable numeric core,
// an all-zero core, or any "-dev"/"dev" suffix.
int mgb_update_version_is_dev(const char *version);

// True if the remote release tag is strictly newer than the current version and
// therefore worth surfacing. Both accept an optional leading v/V and an optional
// "-suffix" (alpha/rc/dev). Rules:
//   - current is dev/unparseable      -> never newer (0)
//   - remote unparseable              -> not newer (0)
//   - remote base X.Y.Z > current     -> newer (a suffixed tag counts iff base
//                                        is greater, e.g. 0.4.0-rc1 over 0.3.3)
//   - equal base, current has suffix
//     and remote has none             -> newer (the full release of a prerelease)
//   - otherwise                       -> not newer
int mgb_update_remote_is_newer(const char *remote_tag, const char *current);

#ifdef __cplusplus
}
#endif

#endif  // MGB64_UPDATE_CHECK_CORE_H
