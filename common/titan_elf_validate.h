#ifndef PROJECT_TITAN_COMMON_ELF_VALIDATE_H
#define PROJECT_TITAN_COMMON_ELF_VALIDATE_H

#include <stddef.h>
#include <stdint.h>

typedef enum TitanElfValidationResult
{
    TITAN_ELF_VALID = 0,
    TITAN_ELF_ERROR_OPEN = -1,
    TITAN_ELF_ERROR_READ = -2,
    TITAN_ELF_ERROR_MAGIC = -3,
    TITAN_ELF_ERROR_FORMAT = -4,
    TITAN_ELF_ERROR_MACHINE = -5,
    TITAN_ELF_ERROR_PROGRAM_HEADERS = -6,
    TITAN_ELF_ERROR_SEGMENT = -7,
    TITAN_ELF_ERROR_ENTRY_POINT = -8,
    TITAN_ELF_ERROR_ALIGNMENT = -9,
    TITAN_ELF_ERROR_OVERLAP = -10,
    TITAN_ELF_ERROR_ADDRESS_RANGE = -11
} TitanElfValidationResult;

typedef struct TitanElfValidationPolicy
{
    uint32_t minimum_address;
    uint32_t maximum_address;
    unsigned int maximum_program_headers;
    int require_executable_entry;
} TitanElfValidationPolicy;

typedef struct TitanElfImageInfo
{
    uint32_t entry_point;
    unsigned int program_header_count;
    unsigned int load_segment_count;
} TitanElfImageInfo;

typedef struct TitanElf32Header
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
} TitanElf32Header;

typedef struct TitanElf32ProgramHeader
{
    uint32_t type;
    uint32_t offset;
    uint32_t virtual_address;
    uint32_t physical_address;
    uint32_t file_size;
    uint32_t memory_size;
    uint32_t flags;
    uint32_t alignment;
} TitanElf32ProgramHeader;

void TitanElf_GetDefaultApplicationPolicy(
    TitanElfValidationPolicy *policy
);

TitanElfValidationResult TitanElf_ValidateFile(
    const char *path
);

TitanElfValidationResult TitanElf_ValidateFileWithPolicy(
    const char *path,
    const TitanElfValidationPolicy *policy,
    TitanElfImageInfo *info
);

TitanElfValidationResult TitanElf_ValidateMemory(
    const void *data,
    size_t size,
    const TitanElfValidationPolicy *policy,
    TitanElfImageInfo *info
);

int TitanElf_ReadMemoryProgramHeader(
    const void *data,
    size_t size,
    unsigned int index,
    TitanElf32ProgramHeader *program
);

const char *TitanElf_GetValidationName(
    TitanElfValidationResult result
);

#endif
