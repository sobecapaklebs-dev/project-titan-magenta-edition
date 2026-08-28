#ifndef PROJECT_TITAN_REPOSITORY_PROTOCOL_H
#define PROJECT_TITAN_REPOSITORY_PROTOCOL_H

#include <stddef.h>

#define TITAN_REPOSITORY_PROTOCOL_VERSION 1u

#define TITAN_REPOSITORY_MAX_PACKAGES 48u
#define TITAN_REPOSITORY_ID_CAPACITY 40u
#define TITAN_REPOSITORY_NAME_CAPACITY 64u
#define TITAN_REPOSITORY_PACKAGE_ID_CAPACITY 40u
#define TITAN_REPOSITORY_TITLE_CAPACITY 80u
#define TITAN_REPOSITORY_VERSION_CAPACITY 24u
#define TITAN_REPOSITORY_URL_CAPACITY 384u
#define TITAN_REPOSITORY_SHA256_CAPACITY 65u
#define TITAN_REPOSITORY_DESCRIPTION_CAPACITY 192u
#define TITAN_REPOSITORY_DEPENDENCIES_CAPACITY 192u
#define TITAN_REPOSITORY_API_LEVEL_CAPACITY 16u

#define TITAN_REPOSITORY_PLATFORM_PS2 "ps2"
#define TITAN_REPOSITORY_ARCH_R5900 "mips-r5900"

typedef struct TitanRepositoryPackage
{
    char package_id[TITAN_REPOSITORY_PACKAGE_ID_CAPACITY];
    char title[TITAN_REPOSITORY_TITLE_CAPACITY];
    char version[TITAN_REPOSITORY_VERSION_CAPACITY];
    char platform[16];
    char architecture[20];
    char download_url[TITAN_REPOSITORY_URL_CAPACITY];
    char sha256[TITAN_REPOSITORY_SHA256_CAPACITY];
    unsigned long long package_size;
    char description[TITAN_REPOSITORY_DESCRIPTION_CAPACITY];
    char icon_url[TITAN_REPOSITORY_URL_CAPACITY];
    char background_url[TITAN_REPOSITORY_URL_CAPACITY];
    char api_level[TITAN_REPOSITORY_API_LEVEL_CAPACITY];
    char dependencies[TITAN_REPOSITORY_DEPENDENCIES_CAPACITY];
} TitanRepositoryPackage;

typedef struct TitanRepositoryCatalog
{
    char repository_id[TITAN_REPOSITORY_ID_CAPACITY];
    char repository_name[TITAN_REPOSITORY_NAME_CAPACITY];
    unsigned int protocol_version;
    unsigned int package_count;
    TitanRepositoryPackage packages[TITAN_REPOSITORY_MAX_PACKAGES];
} TitanRepositoryCatalog;

typedef enum TitanRepositoryParseResult
{
    TITAN_REPOSITORY_PARSE_OK = 0,
    TITAN_REPOSITORY_PARSE_INVALID_ARGUMENT,
    TITAN_REPOSITORY_PARSE_BAD_HEADER,
    TITAN_REPOSITORY_PARSE_BAD_SYNTAX,
    TITAN_REPOSITORY_PARSE_TOO_MANY_PACKAGES,
    TITAN_REPOSITORY_PARSE_DUPLICATE_PACKAGE,
    TITAN_REPOSITORY_PARSE_INVALID_PACKAGE
} TitanRepositoryParseResult;

TitanRepositoryParseResult TitanRepository_Parse(
    const char *text,
    size_t size,
    TitanRepositoryCatalog *out_catalog
);

const TitanRepositoryPackage *TitanRepository_FindPackage(
    const TitanRepositoryCatalog *catalog,
    const char *package_id
);

#endif
