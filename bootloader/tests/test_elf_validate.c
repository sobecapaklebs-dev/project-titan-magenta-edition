#define _POSIX_C_SOURCE 200809L

#include "elf_validate.h"

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct TestElfHeader32
{
    unsigned char ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t program_header_offset;
    uint32_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_size;
    uint16_t program_header_count;
    uint16_t section_header_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} TestElfHeader32;

typedef struct TestElfProgramHeader32
{
    uint32_t type;
    uint32_t offset;
    uint32_t virtual_address;
    uint32_t physical_address;
    uint32_t file_size;
    uint32_t memory_size;
    uint32_t flags;
    uint32_t alignment;
} TestElfProgramHeader32;

static void WriteValidElf(int descriptor)
{
    TestElfHeader32 header;
    TestElfProgramHeader32 program;
    unsigned char padding[44];
    unsigned char payload[16];

    memset(&header, 0, sizeof(header));
    memset(&program, 0, sizeof(program));
    memset(padding, 0, sizeof(padding));
    memset(payload, 0x5a, sizeof(payload));

    header.ident[0] = 0x7f;
    header.ident[1] = 'E';
    header.ident[2] = 'L';
    header.ident[3] = 'F';
    header.ident[4] = 1;
    header.ident[5] = 1;
    header.ident[6] = 1;
    header.type = 2;
    header.machine = 8;
    header.version = 1;
    header.entry = 0x00100000u;
    header.program_header_offset = sizeof(header);
    header.header_size = sizeof(header);
    header.program_header_size = sizeof(program);
    header.program_header_count = 1;

    program.type = 1;
    program.offset = 128u;
    program.virtual_address = 0x00100000u;
    program.physical_address = 0x00100000u;
    program.file_size = sizeof(payload);
    program.memory_size = sizeof(payload) + 16u;
    program.flags = 5;
    program.alignment = 128u;

    assert(write(descriptor, &header, sizeof(header)) == (ssize_t)sizeof(header));
    assert(write(descriptor, &program, sizeof(program)) == (ssize_t)sizeof(program));
    assert(write(descriptor, padding, sizeof(padding)) == (ssize_t)sizeof(padding));
    assert(write(descriptor, payload, sizeof(payload)) == (ssize_t)sizeof(payload));
    assert(fsync(descriptor) == 0);
}

int main(
    int argc,
    char *argv[]
)
{
    TitanElfValidationResult validation;
    TestElfHeader32 header;
    char path[] = "/tmp/titan-elf-test-XXXXXX";
    int descriptor;

    if (argc == 2)
    {
        validation = TitanElf_ValidateFile(argv[1]);
        printf(
            "%s: %s (%d)\n",
            argv[1],
            TitanElf_GetValidationName(validation),
            (int)validation
        );
        return validation == TITAN_ELF_VALID ? 0 : 1;
    }

    descriptor = mkstemp(path);
    assert(descriptor >= 0);

    WriteValidElf(descriptor);
    close(descriptor);

    assert(TitanElf_ValidateFile(path) == TITAN_ELF_VALID);

    descriptor = open(path, O_RDWR);
    assert(descriptor >= 0);
    assert(read(descriptor, &header, sizeof(header)) == (ssize_t)sizeof(header));
    header.ident[0] = 0;
    assert(lseek(descriptor, 0, SEEK_SET) == 0);
    assert(write(descriptor, &header, sizeof(header)) == (ssize_t)sizeof(header));
    close(descriptor);

    assert(TitanElf_ValidateFile(path) == TITAN_ELF_ERROR_MAGIC);
    assert(unlink(path) == 0);
    assert(TitanElf_ValidateFile(path) == TITAN_ELF_ERROR_OPEN);

    puts("test_elf_validate: PASS");
    return 0;
}
