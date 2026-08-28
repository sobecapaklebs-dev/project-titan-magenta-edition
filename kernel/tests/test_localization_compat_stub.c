#include "titan_localization.h"

const char *TitanLocalization_Compat(const char *text)
{
    return text != 0 ? text : "";
}

TitanLanguage TitanLocalization_GetLanguage(void)
{
    return TITAN_LANGUAGE_ENGLISH;
}
