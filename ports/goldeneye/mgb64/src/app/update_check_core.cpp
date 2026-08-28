// update_check_core.cpp — see update_check_core.h. Pure C-style logic, no SDL,
// no network, no allocation: unit-tested in isolation.
#include "update_check_core.h"

#include <ctype.h>
#include <string.h>

namespace {

// Case-insensitive substring search, bounded by an explicit length (the JSON
// body is not guaranteed NUL-terminated at `len`).
const char *findKey(const char *hay, size_t len, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || len < nlen) return nullptr;
    for (size_t i = 0; i + nlen <= len; ++i) {
        if (memcmp(hay + i, needle, nlen) == 0) return hay + i;
    }
    return nullptr;
}

struct SemVer {
    int a, b, c;      // numeric core, missing parts default to 0
    int has_suffix;   // 1 if a "-suffix" was present
    int valid;        // 1 if at least one numeric digit was parsed
};

SemVer parse(const char *v) {
    SemVer out = {0, 0, 0, 0, 0};
    if (!v) return out;
    const char *p = v;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == 'v' || *p == 'V') ++p;

    int *fields[3] = {&out.a, &out.b, &out.c};
    int fi = 0;
    while (*p && fi < 3) {
        if (*p >= '0' && *p <= '9') {
            int val = 0;
            while (*p >= '0' && *p <= '9') {
                val = val * 10 + (*p - '0');
                ++p;
            }
            *fields[fi++] = val;
            out.valid = 1;
            if (*p == '.') { ++p; continue; }
        }
        break;
    }
    // Anything past the numeric core that starts a pre-release marker.
    if (*p == '-' || *p == '+') out.has_suffix = 1;
    return out;
}

// -1 / 0 / +1 on the numeric core only.
int cmpCore(const SemVer &x, const SemVer &y) {
    if (x.a != y.a) return x.a < y.a ? -1 : 1;
    if (x.b != y.b) return x.b < y.b ? -1 : 1;
    if (x.c != y.c) return x.c < y.c ? -1 : 1;
    return 0;
}

// H1 hardening: the release-tag charset allow-list. Applied to the DECODED tag
// (after JSON escape decoding), so "v9.9.9\nadvanced_env=..." — a config-key
// injection against the newline-delimited app ini via Dismiss — is rejected no
// matter how the hostile bytes were encoded. Real tags (v0.3.3, 1.0.0-rc1)
// always fit; anything else is not a version worth banner-ing about.
int tagCharOk(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           c == '.' || c == '_' || c == '+' || c == '-';
}

}  // namespace

int mgb_update_version_is_dev(const char *version) {
    if (!version || !version[0]) return 1;
    // Any "dev" token anywhere (case-insensitive) is a dev build.
    for (const char *p = version; *p; ++p) {
        if ((p[0] == 'd' || p[0] == 'D') &&
            (p[1] == 'e' || p[1] == 'E') &&
            (p[2] == 'v' || p[2] == 'V')) {
            return 1;
        }
    }
    SemVer sv = parse(version);
    if (!sv.valid) return 1;                       // unparseable
    if (sv.a == 0 && sv.b == 0 && sv.c == 0) return 1;  // no real 0.0.0 release
    return 0;
}

int mgb_update_remote_is_newer(const char *remote_tag, const char *current) {
    if (mgb_update_version_is_dev(current)) return 0;   // never nag dev builds
    SemVer r = parse(remote_tag);
    if (!r.valid) return 0;
    SemVer c = parse(current);
    int cc = cmpCore(r, c);
    if (cc > 0) return 1;                                // strictly greater core
    if (cc < 0) return 0;
    // Equal core: only newer if we're on a prerelease of it and remote is the
    // full release (current has a suffix, remote does not).
    if (c.has_suffix && !r.has_suffix) return 1;
    return 0;
}

int mgb_update_extract_tag(const char *json, size_t len, char *out, size_t cap) {
    if (!json || !out || cap == 0) return 0;
    out[0] = '\0';

    const char *k = findKey(json, len, "\"tag_name\"");
    if (!k) return 0;
    const char *end = json + len;
    const char *p = k + strlen("\"tag_name\"");

    // Skip whitespace and the ':' separator.
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    if (p >= end || *p != ':') return 0;
    ++p;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    if (p >= end) return 0;
    if (*p == 'n') return 0;   // null value (e.g. no releases yet)
    if (*p != '"') return 0;
    ++p;

    size_t o = 0;
    while (p < end && *p != '"') {
        char ch = *p;
        if (ch == '\\' && p + 1 < end) {
            ++p;
            switch (*p) {
                case 'n': ch = '\n'; break;
                case 't': ch = '\t'; break;
                case 'r': ch = '\r'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'u':
                    // \uXXXX: skip the 4 hex digits, emit '?' (tags are ASCII).
                    if (p + 4 < end) p += 4;
                    ch = '?';
                    break;
                default:  ch = *p; break;   // \" \\ \/ and anything else: literal
            }
        }
        if (!tagCharOk(ch)) {   // hostile/odd byte => reject the whole tag (H1)
            out[0] = '\0';
            return 0;
        }
        if (o + 1 < cap) out[o++] = ch;
        ++p;
    }
    out[o] = '\0';
    return o > 0 ? 1 : 0;
}
