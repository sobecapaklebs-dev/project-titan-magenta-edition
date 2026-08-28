#ifndef PROJECT_TITAN_DATETIME_H
#define PROJECT_TITAN_DATETIME_H

#include <stdint.h>

#define TITAN_DATETIME_SERVER_CAPACITY 64u
#define TITAN_DATETIME_TEXT_CAPACITY 32u

typedef enum TitanDateTimeSource {
    TITAN_DATETIME_SOURCE_NONE = 0,
    TITAN_DATETIME_SOURCE_RTC,
    TITAN_DATETIME_SOURCE_SNTP,
    TITAN_DATETIME_SOURCE_MANUAL
} TitanDateTimeSource;

typedef enum TitanDateTimeSyncState {
    TITAN_DATETIME_SYNC_IDLE = 0,
    TITAN_DATETIME_SYNC_WAIT_NETWORK,
    TITAN_DATETIME_SYNC_PENDING,
    TITAN_DATETIME_SYNC_RUNNING,
    TITAN_DATETIME_SYNC_OK,
    TITAN_DATETIME_SYNC_DNS_ERROR,
    TITAN_DATETIME_SYNC_SOCKET_ERROR,
    TITAN_DATETIME_SYNC_TIMEOUT,
    TITAN_DATETIME_SYNC_INVALID_RESPONSE
} TitanDateTimeSyncState;

typedef enum TitanDateFormat {
    TITAN_DATE_FORMAT_YMD = 0,
    TITAN_DATE_FORMAT_DMY,
    TITAN_DATE_FORMAT_MDY
} TitanDateFormat;

typedef enum TitanClockFormat {
    TITAN_CLOCK_FORMAT_24H = 0,
    TITAN_CLOCK_FORMAT_12H
} TitanClockFormat;

typedef struct TitanDateTimeSnapshot {
    int valid;
    int automatic;
    int timezone_minutes;
    TitanDateFormat date_format;
    TitanClockFormat clock_format;
    TitanDateTimeSource source;
    TitanDateTimeSyncState sync_state;
    uint64_t utc_epoch_seconds;
    uint64_t local_epoch_seconds;
    uint64_t last_sync_epoch_seconds;
    unsigned int sync_attempts;
    unsigned int sync_successes;
    int last_error;
    char server[TITAN_DATETIME_SERVER_CAPACITY];
    char date_text[TITAN_DATETIME_TEXT_CAPACITY];
    char time_text[TITAN_DATETIME_TEXT_CAPACITY];
} TitanDateTimeSnapshot;

void TitanDateTime_Initialize(void);
void TitanDateTime_Shutdown(void);
void TitanDateTime_Update(int network_online, uint64_t now_ms);
void TitanDateTime_RequestSync(void);
void TitanDateTime_SetAutomatic(int enabled);
void TitanDateTime_SetTimezoneMinutes(int minutes);
void TitanDateTime_CycleTimezone(int direction);
void TitanDateTime_SetClockFormat(TitanClockFormat format);
void TitanDateTime_ToggleClockFormat(void);
void TitanDateTime_SetDateFormat(TitanDateFormat format);
void TitanDateTime_CycleDateFormat(int direction);
int TitanDateTime_AdjustManualSeconds(int64_t delta_seconds);
int TitanDateTime_AdjustManualDays(int delta_days);
const TitanDateTimeSnapshot *TitanDateTime_GetSnapshot(void);
const char *TitanDateTime_GetSyncStateName(void);
const char *TitanDateTime_GetSourceName(void);
const char *TitanDateTime_GetTimezoneText(void);
const char *TitanDateTime_GetDateFormatName(void);
const char *TitanDateTime_GetClockFormatName(void);

/* Pure helpers are intentionally public to support host conformance tests and
 * SDK-facing formatting without exposing CD/DVD RTC or socket ownership. */
int TitanDateTime_ValidateCivil(int year, int month, int day, int hour, int minute, int second);
uint64_t TitanDateTime_CivilToUnix(int year, int month, int day, int hour, int minute, int second);
int TitanDateTime_UnixToCivil(uint64_t epoch, int *year, int *month, int *day, int *hour, int *minute, int *second);
int TitanDateTime_ParseSntpResponse(const unsigned char *packet, unsigned int size, uint32_t request_nonce, uint64_t *unix_seconds);

#endif
