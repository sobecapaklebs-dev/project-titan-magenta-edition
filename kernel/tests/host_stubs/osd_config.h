#ifndef TITAN_HOST_STUB_OSD_CONFIG_H
#define TITAN_HOST_STUB_OSD_CONFIG_H

enum VIDEO_OUTPUT_TYPES
{
    VIDEO_OUTPUT_RGB = 0,
    VIDEO_OUTPUT_COMPONENT
};

typedef struct ConfigParam
{
    unsigned int spdifMode : 1;
    unsigned int screenType : 2;
    unsigned int videoOutput : 1;
    unsigned int japLanguage : 1;
    unsigned int ps1drvConfig : 8;
    unsigned int version : 3;
    unsigned int language : 5;
    unsigned int timezoneOffset : 11;
} ConfigParam;

int GetOsdConfigParam(ConfigParam *config);

#endif
