#include "titan_sas.h"

#include <ctype.h>
#include <string.h>

/*
 * Canonical table deliberately contains only prefixes backed by the audited
 * PS2 Application System primary material. Unknown three-letter prefixes may
 * be compatibility hints, but are never promoted into this normative table.
 */
static const TitanSasPrefixRule g_titan_sas_prefix_rules[] = {
    {
        "APP",
        TITAN_SAS_PREFIX_CATEGORY_APPLICATION,
        TITAN_SAS_PREFIX_SOURCE_TNA_PRIMARY,
        TITAN_SAS_CONFIDENCE_HIGH,
        "TnA SAS Standard example/default; most application folders use APP"
    }
};

unsigned int TitanSas_GetPrefixRuleCount(void)
{
    return (unsigned int)(sizeof(g_titan_sas_prefix_rules) /
        sizeof(g_titan_sas_prefix_rules[0]));
}

const TitanSasPrefixRule *TitanSas_GetPrefixRule(unsigned int index)
{
    if (index >= TitanSas_GetPrefixRuleCount())
        return 0;
    return &g_titan_sas_prefix_rules[index];
}

const TitanSasPrefixRule *TitanSas_FindPrefixRule(const char *prefix, unsigned int length)
{
    unsigned int index;
    if (prefix == 0 || length == 0u)
        return 0;
    for (index = 0u; index < TitanSas_GetPrefixRuleCount(); index++)
    {
        const char *candidate = g_titan_sas_prefix_rules[index].prefix;
        unsigned int i;
        if (strlen(candidate) != length)
            continue;
        for (i = 0u; i < length; i++)
        {
            if (toupper((unsigned char)prefix[i]) !=
                toupper((unsigned char)candidate[i]))
                break;
        }
        if (i == length)
            return &g_titan_sas_prefix_rules[index];
    }
    return 0;
}

int TitanSas_IsThreeLetterCompatibilityPrefix(const char *prefix, unsigned int length)
{
    unsigned int i;
    if (prefix == 0 || length != 3u)
        return 0;
    for (i = 0u; i < length; i++)
    {
        if (!isalnum((unsigned char)prefix[i]))
            return 0;
    }
    return 1;
}
