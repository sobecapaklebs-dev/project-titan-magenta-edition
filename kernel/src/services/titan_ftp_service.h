#ifndef PROJECT_TITAN_FTP_SERVICE_H
#define PROJECT_TITAN_FTP_SERVICE_H

typedef enum TitanFtpServiceState
{
    TITAN_FTP_STATE_STOPPED = 0,
    TITAN_FTP_STATE_LISTENING,
    TITAN_FTP_STATE_CONNECTED
} TitanFtpServiceState;

typedef struct TitanFtpServiceSnapshot
{
    TitanFtpServiceState state;
    unsigned int sessions_accepted;
    unsigned int commands_processed;
    unsigned int commands_rejected;
    unsigned int protocol_errors;
    unsigned short control_port;
    char root[128];
    char cwd[128];
} TitanFtpServiceSnapshot;

int TitanFtpService_Start(const char *root, unsigned short port);
void TitanFtpService_Tick(void);
void TitanFtpService_Stop(void);
void TitanFtpService_GetSnapshot(TitanFtpServiceSnapshot *snapshot);

#endif
