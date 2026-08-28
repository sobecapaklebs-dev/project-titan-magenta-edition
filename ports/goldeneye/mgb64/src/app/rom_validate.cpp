// rom_validate.cpp — see rom_validate.h.
#include "rom_validate.h"

#include <cstdio>
#include <cstring>
#include <cctype>

namespace {

// Detect N64 byte order from the first two header bytes.
const char *detectByteOrder(const unsigned char *h) {
    if (h[0] == 0x80 && h[1] == 0x37) return "z64";  // big-endian
    if (h[0] == 0x37 && h[1] == 0x80) return "v64";  // byte-swapped
    if (h[0] == 0x40 && h[1] == 0x12) return "n64";  // little-endian
    return "???";
}

// Normalize a 64-byte header into big-endian (.z64) order in place.
void toBigEndian(unsigned char *h, const char *order) {
    if (std::strcmp(order, "v64") == 0) {
        for (int i = 0; i + 1 < 64; i += 2) {
            unsigned char t = h[i]; h[i] = h[i + 1]; h[i + 1] = t;
        }
    } else if (std::strcmp(order, "n64") == 0) {
        for (int i = 0; i + 3 < 64; i += 4) {
            unsigned char a = h[i], b = h[i + 1];
            h[i] = h[i + 3]; h[i + 1] = h[i + 2]; h[i + 2] = b; h[i + 3] = a;
        }
    }
}

const char *regionFromCountry(unsigned char code) {
    switch (code) {
        case 'E': return "US";  // North America
        case 'P': return "EU";  // Europe (PAL)
        case 'J': return "JP";  // Japan
        default:  return "??";
    }
}

}  // namespace

extern "C" RomInfo mgb_validate_rom(const char *path) {
    RomInfo info;
    std::memset(&info, 0, sizeof(info));
    std::strncpy(info.byte_order, "???", sizeof(info.byte_order) - 1);
    std::strncpy(info.region, "??", sizeof(info.region) - 1);

    if (!path || !path[0]) {
        std::snprintf(info.message, sizeof(info.message), "No file selected.");
        return info;
    }

    std::FILE *f = std::fopen(path, "rb");
    if (!f) {
        std::snprintf(info.message, sizeof(info.message), "Cannot open file.");
        return info;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    // GoldenEye 007 is a 12 MB (96 Mbit) cartridge in every region (U/E/J). The
    // engine boot rejects anything else (rom_io.c GE007_ROM_SIZE_BYTES == 0xC00000),
    // so require the exact size here too rather than mark an unbootable image
    // "Ready to play" [AUDIT-0045].
    if (size != 0xC00000) {
        std::fclose(f);
        std::snprintf(info.message, sizeof(info.message),
                      "Unexpected size: %.2f MB (GoldenEye 007 is exactly 12 MB).",
                      (double)size / (1024.0 * 1024.0));
        return info;
    }
    info.size_bytes = (unsigned)size;

    unsigned char h[64];
    if (std::fread(h, 1, sizeof(h), f) != sizeof(h)) {
        std::fclose(f);
        std::snprintf(info.message, sizeof(info.message), "Cannot read ROM header.");
        return info;
    }
    std::fclose(f);

    const char *order = detectByteOrder(h);
    std::strncpy(info.byte_order, order, sizeof(info.byte_order) - 1);
    if (std::strcmp(order, "???") == 0) {
        std::snprintf(info.message, sizeof(info.message),
                      "Not a recognized N64 ROM (bad magic).");
        return info;
    }

    toBigEndian(h, order);

    // Internal image name occupies header bytes 0x20..0x33.
    char name[21];
    std::memcpy(name, h + 0x20, 20);
    name[20] = '\0';
    for (int i = 19; i >= 0 && (name[i] == ' ' || name[i] == '\0'); --i) name[i] = '\0';
    std::strncpy(info.title, name, sizeof(info.title) - 1);

    std::strncpy(info.region, regionFromCountry(h[0x3E]), sizeof(info.region) - 1);

    // Case-insensitive "GOLDENEYE" signature in the title.
    char upper[21];
    for (int i = 0; i < 20; ++i) upper[i] = (char)std::toupper((unsigned char)name[i]);
    upper[20] = '\0';
    if (std::strstr(upper, "GOLDENEYE") != nullptr) {
        info.valid = 1;
        std::snprintf(info.message, sizeof(info.message),
                      "GoldenEye 007 (%s) — %s, %.1f MB. Ready to play.",
                      info.region, info.byte_order, (double)size / (1024.0 * 1024.0));
    } else {
        info.valid = 0;
        std::snprintf(info.message, sizeof(info.message),
                      "Valid N64 ROM but not GoldenEye (title: \"%s\").", info.title);
    }
    return info;
}
