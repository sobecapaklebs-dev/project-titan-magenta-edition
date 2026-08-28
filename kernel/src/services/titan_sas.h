#ifndef PROJECT_TITAN_SAS_H
#define PROJECT_TITAN_SAS_H

typedef enum TitanSasPrefixCategory
{
    TITAN_SAS_PREFIX_CATEGORY_APPLICATION = 0,
    TITAN_SAS_PREFIX_CATEGORY_UNKNOWN = 1
} TitanSasPrefixCategory;

typedef enum TitanSasPrefixSource
{
    TITAN_SAS_PREFIX_SOURCE_TNA_PRIMARY = 0,
    TITAN_SAS_PREFIX_SOURCE_COMPAT_PATTERN = 1
} TitanSasPrefixSource;

typedef enum TitanSasConfidence
{
    TITAN_SAS_CONFIDENCE_LOW = 0,
    TITAN_SAS_CONFIDENCE_MEDIUM = 1,
    TITAN_SAS_CONFIDENCE_HIGH = 2
} TitanSasConfidence;

typedef enum TitanSasProfile
{
    TITAN_SAS_PROFILE_NONE = 0,
    TITAN_SAS_PROFILE_CLASSIC = 1,
    TITAN_SAS_PROFILE_EXTENDED_COMPAT = 2,
    TITAN_SAS_PROFILE_TITAN_FALLBACK = 3
} TitanSasProfile;

typedef enum TitanSasCopyability
{
    TITAN_SAS_COPY_UNKNOWN = 0,
    TITAN_SAS_OSDSYS_COPY_SAFE = 1,
    TITAN_SAS_OSDSYS_COPY_UNSAFE = 2
} TitanSasCopyability;

typedef struct TitanSasPrefixRule
{
    const char *prefix;
    TitanSasPrefixCategory category;
    TitanSasPrefixSource source;
    TitanSasConfidence confidence;
    const char *notes;
} TitanSasPrefixRule;

unsigned int TitanSas_GetPrefixRuleCount(void);
const TitanSasPrefixRule *TitanSas_GetPrefixRule(unsigned int index);
const TitanSasPrefixRule *TitanSas_FindPrefixRule(const char *prefix, unsigned int length);
int TitanSas_IsThreeLetterCompatibilityPrefix(const char *prefix, unsigned int length);

#endif
