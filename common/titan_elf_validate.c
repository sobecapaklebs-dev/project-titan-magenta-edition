#include "titan_elf_validate.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define TITAN_ELF_CLASS_32 1u
#define TITAN_ELF_DATA_LITTLE_ENDIAN 1u
#define TITAN_ELF_VERSION_CURRENT 1u
#define TITAN_ELF_TYPE_EXECUTABLE 2u
#define TITAN_ELF_MACHINE_MIPS 8u
#define TITAN_ELF_PROGRAM_LOAD 1u
#define TITAN_ELF_PROGRAM_FLAG_EXECUTE 0x1u
#define TITAN_ELF_DEFAULT_MINIMUM_ADDRESS 0x00100000u
#define TITAN_ELF_DEFAULT_MAXIMUM_ADDRESS 0x02000000u
#define TITAN_ELF_DEFAULT_MAXIMUM_PROGRAM_HEADERS 128u
#define TITAN_ELF_INTERNAL_MAXIMUM_PROGRAM_HEADERS 256u

typedef int (*TitanElfReadAt)(
    void *context,
    uint64_t offset,
    void *buffer,
    size_t size
);

typedef struct TitanElfFileReader
{
    int descriptor;
} TitanElfFileReader;

typedef struct TitanElfMemoryReader
{
    const unsigned char *data;
    size_t size;
} TitanElfMemoryReader;

typedef struct TitanElfLoadRange
{
    uint64_t start;
    uint64_t end;
} TitanElfLoadRange;

static int TitanElf_IsPowerOfTwo(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static int TitanElf_FileReadAt(
    void *context,
    uint64_t offset,
    void *buffer,
    size_t size
)
{
    TitanElfFileReader *reader;
    unsigned char *cursor;
    size_t remaining;
    off_t native_offset;

    reader = (TitanElfFileReader *)context;
    native_offset = (off_t)offset;
    if ((uint64_t)native_offset != offset)
    {
        return 0;
    }

    if (lseek(reader->descriptor, native_offset, SEEK_SET) < 0)
    {
        return 0;
    }

    cursor = (unsigned char *)buffer;
    remaining = size;
    while (remaining > 0u)
    {
        ssize_t result;

        do
        {
            result = read(reader->descriptor, cursor, remaining);
        }
        while (result < 0 && errno == EINTR);

        if (result <= 0 || (size_t)result > remaining)
        {
            return 0;
        }

        cursor += (size_t)result;
        remaining -= (size_t)result;
    }

    return 1;
}

static int TitanElf_MemoryReadAt(
    void *context,
    uint64_t offset,
    void *buffer,
    size_t size
)
{
    TitanElfMemoryReader *reader;
    uint64_t end;

    reader = (TitanElfMemoryReader *)context;
    end = offset + (uint64_t)size;
    if (
        end < offset ||
        end > (uint64_t)reader->size
    )
    {
        return 0;
    }

    memcpy(buffer, reader->data + (size_t)offset, size);
    return 1;
}

static TitanElfValidationResult TitanElf_ValidateReader(
    TitanElfReadAt read_at,
    void *context,
    uint64_t file_size,
    const TitanElfValidationPolicy *requested_policy,
    TitanElfImageInfo *info
)
{
    TitanElfValidationPolicy default_policy;
    const TitanElfValidationPolicy *policy;
    TitanElf32Header header;
    TitanElfLoadRange ranges[TITAN_ELF_INTERNAL_MAXIMUM_PROGRAM_HEADERS];
    uint64_t program_table_size;
    uint64_t program_table_end;
    unsigned int index;
    unsigned int range_count;
    int entry_covered;

    if (info != 0)
    {
        memset(info, 0, sizeof(*info));
    }

    TitanElf_GetDefaultApplicationPolicy(&default_policy);
    policy = requested_policy != 0 ? requested_policy : &default_policy;

    if (
        policy->minimum_address >= policy->maximum_address ||
        policy->maximum_program_headers == 0u ||
        policy->maximum_program_headers > TITAN_ELF_INTERNAL_MAXIMUM_PROGRAM_HEADERS
    )
    {
        return TITAN_ELF_ERROR_ADDRESS_RANGE;
    }

    if (
        file_size < sizeof(header) ||
        !read_at(context, 0u, &header, sizeof(header))
    )
    {
        return TITAN_ELF_ERROR_READ;
    }

    if (
        header.ident[0] != 0x7fu ||
        header.ident[1] != 'E' ||
        header.ident[2] != 'L' ||
        header.ident[3] != 'F'
    )
    {
        return TITAN_ELF_ERROR_MAGIC;
    }

    if (
        header.ident[4] != TITAN_ELF_CLASS_32 ||
        header.ident[5] != TITAN_ELF_DATA_LITTLE_ENDIAN ||
        header.ident[6] != TITAN_ELF_VERSION_CURRENT ||
        header.type != TITAN_ELF_TYPE_EXECUTABLE ||
        header.version != TITAN_ELF_VERSION_CURRENT ||
        header.header_size != sizeof(TitanElf32Header)
    )
    {
        return TITAN_ELF_ERROR_FORMAT;
    }

    if (header.machine != TITAN_ELF_MACHINE_MIPS)
    {
        return TITAN_ELF_ERROR_MACHINE;
    }

    if (
        header.program_header_count == 0u ||
        header.program_header_count > policy->maximum_program_headers ||
        header.program_header_size != sizeof(TitanElf32ProgramHeader)
    )
    {
        return TITAN_ELF_ERROR_PROGRAM_HEADERS;
    }

    program_table_size =
        (uint64_t)header.program_header_count *
        (uint64_t)header.program_header_size;
    program_table_end =
        (uint64_t)header.program_header_offset + program_table_size;

    if (
        program_table_end < (uint64_t)header.program_header_offset ||
        program_table_end > file_size
    )
    {
        return TITAN_ELF_ERROR_PROGRAM_HEADERS;
    }

    range_count = 0u;
    entry_covered = 0;

    for (index = 0u; index < header.program_header_count; index++)
    {
        TitanElf32ProgramHeader program;
        uint64_t program_offset;
        uint64_t segment_file_end;
        uint64_t segment_memory_end;
        unsigned int previous;

        program_offset =
            (uint64_t)header.program_header_offset +
            ((uint64_t)index * sizeof(program));
        if (!read_at(context, program_offset, &program, sizeof(program)))
        {
            return TITAN_ELF_ERROR_PROGRAM_HEADERS;
        }

        if (program.type != TITAN_ELF_PROGRAM_LOAD)
        {
            continue;
        }

        if (
            program.memory_size == 0u ||
            program.file_size > program.memory_size
        )
        {
            return TITAN_ELF_ERROR_SEGMENT;
        }

        segment_file_end =
            (uint64_t)program.offset + (uint64_t)program.file_size;
        segment_memory_end =
            (uint64_t)program.virtual_address + (uint64_t)program.memory_size;

        if (
            segment_file_end < (uint64_t)program.offset ||
            segment_file_end > file_size
        )
        {
            return TITAN_ELF_ERROR_SEGMENT;
        }

        if (
            segment_memory_end < (uint64_t)program.virtual_address ||
            program.virtual_address < policy->minimum_address ||
            segment_memory_end > (uint64_t)policy->maximum_address
        )
        {
            return TITAN_ELF_ERROR_ADDRESS_RANGE;
        }

        if (
            program.alignment > 1u &&
            (
                !TitanElf_IsPowerOfTwo(program.alignment) ||
                (program.virtual_address % program.alignment) !=
                    (program.offset % program.alignment)
            )
        )
        {
            return TITAN_ELF_ERROR_ALIGNMENT;
        }

        for (previous = 0u; previous < range_count; previous++)
        {
            if (
                (uint64_t)program.virtual_address < ranges[previous].end &&
                segment_memory_end > ranges[previous].start
            )
            {
                return TITAN_ELF_ERROR_OVERLAP;
            }
        }

        ranges[range_count].start = program.virtual_address;
        ranges[range_count].end = segment_memory_end;
        range_count++;

        if (
            header.entry >= program.virtual_address &&
            (uint64_t)header.entry < segment_memory_end &&
            (
                !policy->require_executable_entry ||
                (program.flags & TITAN_ELF_PROGRAM_FLAG_EXECUTE) != 0u
            )
        )
        {
            entry_covered = 1;
        }
    }

    if (range_count == 0u)
    {
        return TITAN_ELF_ERROR_SEGMENT;
    }

    if (!entry_covered)
    {
        return TITAN_ELF_ERROR_ENTRY_POINT;
    }

    if (info != 0)
    {
        info->entry_point = header.entry;
        info->program_header_count = header.program_header_count;
        info->load_segment_count = range_count;
    }

    return TITAN_ELF_VALID;
}

void TitanElf_GetDefaultApplicationPolicy(
    TitanElfValidationPolicy *policy
)
{
    if (policy == 0)
    {
        return;
    }

    policy->minimum_address = TITAN_ELF_DEFAULT_MINIMUM_ADDRESS;
    policy->maximum_address = TITAN_ELF_DEFAULT_MAXIMUM_ADDRESS;
    policy->maximum_program_headers =
        TITAN_ELF_DEFAULT_MAXIMUM_PROGRAM_HEADERS;
    policy->require_executable_entry = 1;
}

TitanElfValidationResult TitanElf_ValidateFile(
    const char *path
)
{
    return TitanElf_ValidateFileWithPolicy(path, 0, 0);
}

TitanElfValidationResult TitanElf_ValidateFileWithPolicy(
    const char *path,
    const TitanElfValidationPolicy *policy,
    TitanElfImageInfo *info
)
{
    TitanElfFileReader reader;
    struct stat status;
    TitanElfValidationResult result;

    if (path == 0 || path[0] == '\0')
    {
        return TITAN_ELF_ERROR_OPEN;
    }

    reader.descriptor = open(path, O_RDONLY);
    if (reader.descriptor < 0)
    {
        return TITAN_ELF_ERROR_OPEN;
    }

    if (fstat(reader.descriptor, &status) < 0 || status.st_size < 0)
    {
        close(reader.descriptor);
        return TITAN_ELF_ERROR_READ;
    }

    result = TitanElf_ValidateReader(
        TitanElf_FileReadAt,
        &reader,
        (uint64_t)status.st_size,
        policy,
        info
    );

    close(reader.descriptor);
    return result;
}

TitanElfValidationResult TitanElf_ValidateMemory(
    const void *data,
    size_t size,
    const TitanElfValidationPolicy *policy,
    TitanElfImageInfo *info
)
{
    TitanElfMemoryReader reader;

    if (data == 0)
    {
        return TITAN_ELF_ERROR_READ;
    }

    reader.data = (const unsigned char *)data;
    reader.size = size;

    return TitanElf_ValidateReader(
        TitanElf_MemoryReadAt,
        &reader,
        (uint64_t)size,
        policy,
        info
    );
}

int TitanElf_ReadMemoryProgramHeader(
    const void *data,
    size_t size,
    unsigned int index,
    TitanElf32ProgramHeader *program
)
{
    TitanElfMemoryReader reader;
    TitanElf32Header header;
    uint64_t offset;

    if (data == 0 || program == 0 || size < sizeof(header))
    {
        return 0;
    }

    reader.data = (const unsigned char *)data;
    reader.size = size;
    if (!TitanElf_MemoryReadAt(&reader, 0u, &header, sizeof(header)))
    {
        return 0;
    }

    if (
        index >= header.program_header_count ||
        header.program_header_size != sizeof(*program)
    )
    {
        return 0;
    }

    offset =
        (uint64_t)header.program_header_offset +
        ((uint64_t)index * sizeof(*program));
    return TitanElf_MemoryReadAt(&reader, offset, program, sizeof(*program));
}

const char *TitanElf_GetValidationName(
    TitanElfValidationResult result
)
{
    switch (result)
    {
        case TITAN_ELF_VALID:
            return "valid";
        case TITAN_ELF_ERROR_OPEN:
            return "not found";
        case TITAN_ELF_ERROR_READ:
            return "read failure";
        case TITAN_ELF_ERROR_MAGIC:
            return "invalid ELF magic";
        case TITAN_ELF_ERROR_FORMAT:
            return "unsupported ELF format";
        case TITAN_ELF_ERROR_MACHINE:
            return "not a MIPS ELF";
        case TITAN_ELF_ERROR_PROGRAM_HEADERS:
            return "invalid program headers";
        case TITAN_ELF_ERROR_SEGMENT:
            return "unsafe ELF segment";
        case TITAN_ELF_ERROR_ENTRY_POINT:
            return "invalid executable entry point";
        case TITAN_ELF_ERROR_ALIGNMENT:
            return "invalid segment alignment";
        case TITAN_ELF_ERROR_OVERLAP:
            return "overlapping load segments";
        case TITAN_ELF_ERROR_ADDRESS_RANGE:
            return "segment outside allowed address range";
        default:
            return "unknown validation failure";
    }
}
