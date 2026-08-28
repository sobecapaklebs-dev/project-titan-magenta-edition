// rom_validate.h — portable ROM validation for the app shell's ROM picker.
//
// Unlike GameBridge.c's validator (macOS CommonCrypto SHA1), this is fully
// portable: it inspects only the 64-byte N64 header (byte order, internal
// title, country code), so it works identically on macOS/Windows/Linux and
// doesn't read the whole 12 MB ROM. Region comes from the header country code.
#ifndef MGB64_ROM_VALIDATE_H
#define MGB64_ROM_VALIDATE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  valid;             // 1 if this looks like a GoldenEye N64 ROM
    char byte_order[4];     // "z64" | "v64" | "n64" | "???"
    char region[4];         // "US" | "EU" | "JP" | "??"
    char title[24];         // internal ROM image name
    unsigned size_bytes;
    char message[160];      // human-readable status
} RomInfo;

// Validate the file at `path`. Never modifies global state; safe on any thread.
RomInfo mgb_validate_rom(const char *path);

#ifdef __cplusplus
}
#endif

#endif  // MGB64_ROM_VALIDATE_H
