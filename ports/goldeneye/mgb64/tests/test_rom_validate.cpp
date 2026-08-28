// test_rom_validate.cpp — unit test for the portable ROM validator.
#include "rom_validate.h"

#include <cstdio>
#include <cstring>
#include <vector>

// Write a synthetic ROM: big-endian magic b0/b1, internal title at 0x20,
// country code at 0x3E, zero-padded to `size` bytes.
static void writeRom(const char *path, unsigned char b0, unsigned char b1,
                     const char *title, unsigned char country, long size) {
    std::vector<unsigned char> buf((size_t)size, 0);
    buf[0] = b0;
    buf[1] = b1;
    if (title) std::memcpy(&buf[0x20], title, std::strlen(title));
    buf[0x3E] = country;
    FILE *f = std::fopen(path, "wb");
    std::fwrite(buf.data(), 1, (size_t)size, f);
    std::fclose(f);
}

int main() {
    int fails = 0;
    const char *p = "test_rom_tmp.bin";
    const long kSize = 12L * 1024 * 1024;  // GoldenEye US size

    auto expect = [&](const char *name, bool cond) {
        if (!cond) { std::printf("FAIL: %s\n", name); ++fails; }
    };

    // Valid GoldenEye US, z64.
    writeRom(p, 0x80, 0x37, "GOLDENEYE", 'E', kSize);
    RomInfo a = mgb_validate_rom(p);
    expect("valid GE accepted", a.valid != 0);
    expect("region US", std::strcmp(a.region, "US") == 0);
    expect("byte_order z64", std::strcmp(a.byte_order, "z64") == 0);

    // Valid N64 ROM but not GoldenEye.
    writeRom(p, 0x80, 0x37, "SUPER MARIO 64", 'E', kSize);
    expect("non-GE rejected", mgb_validate_rom(p).valid == 0);

    // Too small.
    writeRom(p, 0x80, 0x37, "GOLDENEYE", 'E', 2048);
    expect("tiny rejected", mgb_validate_rom(p).valid == 0);

    // Not an N64 ROM (bad magic).
    writeRom(p, 0x12, 0x34, "GOLDENEYE", 'E', kSize);
    RomInfo bad = mgb_validate_rom(p);
    expect("bad-magic rejected", bad.valid == 0);
    expect("bad-magic byte_order ???", std::strcmp(bad.byte_order, "???") == 0);

    // v64 byte order is detected.
    writeRom(p, 0x37, 0x80, "", ' ', kSize);
    expect("v64 detected", std::strcmp(mgb_validate_rom(p).byte_order, "v64") == 0);

    // Missing file.
    std::remove(p);
    expect("missing file rejected", mgb_validate_rom(p).valid == 0);

    if (fails == 0) {
        std::printf("PASS: all rom_validate cases\n");
        return 0;
    }
    std::printf("%d failure(s)\n", fails);
    return 1;
}
