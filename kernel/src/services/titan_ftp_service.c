#include "titan_ftp_service.h"
#include "titan_network_stream.h"
#include "titan_storage_io.h"
#include "titan_network.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define TITAN_FTP_DEFAULT_PORT 2121u
#define TITAN_FTP_CONTROL_BUFFER 512u
#define TITAN_FTP_PATH_CAPACITY 128u
#define TITAN_FTP_TICK_DEADLINE_MS 1u
#define TITAN_FTP_PASSIVE_PORT_BASE 50000u
#define TITAN_FTP_PASSIVE_PORT_SPAN 32u
#define TITAN_FTP_FILE_CHUNK 4096u

static TitanNetworkStreamHandle g_listener;
static TitanNetworkStreamHandle g_control;
static TitanNetworkStreamHandle g_data_listener;
static TitanNetworkStreamHandle g_data_stream;
static TitanFtpServiceSnapshot g_snapshot;
static char g_control_buffer[TITAN_FTP_CONTROL_BUFFER];
static unsigned int g_control_used;
static int g_logged_in;
static unsigned short g_passive_port;
static unsigned int g_list_index;
static int g_list_pending;
static int g_list_names_only;
static char g_list_path[TITAN_FTP_PATH_CAPACITY * 2u];
static int g_file_mode; /* 0 none, 1 RETR, 2 STOR */
static int g_file_fd = -1;
static char g_file_target[TITAN_FTP_PATH_CAPACITY * 2u];
static char g_file_temp[TITAN_FTP_PATH_CAPACITY * 2u + 16u];
static unsigned char g_file_buffer[TITAN_FTP_FILE_CHUNK];
static int TitanFtp_Send(const char *text);


static void TitanFtp_CloseData(void)
{
    if (g_data_stream != TITAN_NETWORK_STREAM_INVALID_HANDLE) TitanNetworkStream_Close(g_data_stream);
    if (g_data_listener != TITAN_NETWORK_STREAM_INVALID_HANDLE) TitanNetworkStream_Close(g_data_listener);
    g_data_stream = TITAN_NETWORK_STREAM_INVALID_HANDLE;
    g_data_listener = TITAN_NETWORK_STREAM_INVALID_HANDLE;
    g_list_pending = 0;
    g_list_index = 0u;
}

static int TitanFtp_OpenPassive(void)
{
    unsigned int attempt;
    TitanFtp_CloseData();
    for (attempt = 0u; attempt < TITAN_FTP_PASSIVE_PORT_SPAN; ++attempt)
    {
        unsigned short port = (unsigned short)(TITAN_FTP_PASSIVE_PORT_BASE +
            ((g_passive_port - TITAN_FTP_PASSIVE_PORT_BASE + 1u + attempt) % TITAN_FTP_PASSIVE_PORT_SPAN));
        if (TitanNetworkStream_Listen(TITAN_NETWORK_STREAM_OWNER_FTP, port, 1u, &g_data_listener))
        {
            g_passive_port = port;
            return 1;
        }
    }
    return 0;
}

static int TitanFtp_ParseIpv4(const char *text, unsigned int octets[4])
{
    unsigned int a,b,c,d; char tail;
    if (text == 0 || sscanf(text, "%u.%u.%u.%u%c", &a,&b,&c,&d,&tail) != 4) return 0;
    if (a>255u||b>255u||c>255u||d>255u) return 0;
    octets[0]=a; octets[1]=b; octets[2]=c; octets[3]=d; return 1;
}

static void TitanFtp_TickList(void)
{
    TitanStorageIoEntry entry;
    char line[256];
    unsigned int sent = 0u;
    int count;
    if (!g_list_pending) return;
    if (g_data_stream == TITAN_NETWORK_STREAM_INVALID_HANDLE)
    {
        if (!TitanNetworkStream_Accept(g_data_listener, TITAN_FTP_TICK_DEADLINE_MS, &g_data_stream)) return;
    }
    count = TitanStorageIo_ListDirectory(g_list_path, g_list_index, &entry, 1u);
    if (count < 0)
    {
        TitanFtp_CloseData();
        (void)TitanFtp_Send("451 Directory read failed.\r\n");
        return;
    }
    if (count == 0)
    {
        TitanFtp_CloseData();
        (void)TitanFtp_Send("226 Directory transfer complete.\r\n");
        return;
    }
    if (g_list_names_only)
        (void)snprintf(line, sizeof(line), "%s\r\n", entry.name);
    else
        (void)snprintf(line, sizeof(line), "%c--------- 1 titan titan %llu Jan 01 00:00 %s\r\n",
            (entry.flags & TITAN_STORAGE_IO_ENTRY_DIRECTORY) != 0u ? 'd' : '-',
            (unsigned long long)entry.size, entry.name);
    if (!TitanNetworkStream_Write(g_data_stream, line, (unsigned int)strlen(line), 1000u, &sent) || sent != strlen(line))
    {
        TitanFtp_CloseData();
        (void)TitanFtp_Send("426 Data connection failed.\r\n");
        return;
    }
    g_list_index++;
}


static void TitanFtp_CloseFileTransfer(int remove_temp)
{
    if (g_file_fd >= 0) TitanStorageIo_Close(g_file_fd);
    g_file_fd = -1;
    if (remove_temp && g_file_temp[0] != '\0') (void)TitanStorageIo_RemoveAny(g_file_temp);
    g_file_mode = 0;
    g_file_target[0] = '\0';
    g_file_temp[0] = '\0';
}

static void TitanFtp_TickFile(void)
{
    unsigned int amount = 0u;
    if (g_file_mode == 0) return;
    if (g_data_stream == TITAN_NETWORK_STREAM_INVALID_HANDLE)
    {
        if (!TitanNetworkStream_Accept(g_data_listener, TITAN_FTP_TICK_DEADLINE_MS, &g_data_stream)) return;
    }
    if (g_file_mode == 1)
    {
        TitanStorageIoResult got = TitanStorageIo_ReadSome(g_file_fd, g_file_buffer, sizeof(g_file_buffer));
        if (got < 0) { TitanFtp_CloseFileTransfer(0); TitanFtp_CloseData(); (void)TitanFtp_Send("451 File read failed.\r\n"); return; }
        if (got == 0) { TitanFtp_CloseFileTransfer(0); TitanFtp_CloseData(); (void)TitanFtp_Send("226 File transfer complete.\r\n"); return; }
        if (!TitanNetworkStream_Write(g_data_stream, g_file_buffer, (unsigned int)got, 1000u, &amount) || amount != (unsigned int)got)
        { TitanFtp_CloseFileTransfer(0); TitanFtp_CloseData(); (void)TitanFtp_Send("426 Data connection failed.\r\n"); return; }
        return;
    }
    {
        TitanNetworkStreamIoStatus status = TitanNetworkStream_ReadStatus(g_data_stream, g_file_buffer, sizeof(g_file_buffer), TITAN_FTP_TICK_DEADLINE_MS, &amount);
        if (status == TITAN_NETWORK_STREAM_IO_IDLE) return;
        if (status == TITAN_NETWORK_STREAM_IO_DATA)
        {
            if (!TitanStorageIo_WriteExact(g_file_fd, g_file_buffer, amount))
            { TitanFtp_CloseFileTransfer(1); TitanFtp_CloseData(); (void)TitanFtp_Send("451 File write failed.\r\n"); }
            return;
        }
        if (status == TITAN_NETWORK_STREAM_IO_CLOSED)
        {
            TitanStorageIo_Close(g_file_fd); g_file_fd = -1;
            if (!TitanStorageIo_AtomicReplace(g_file_temp, g_file_target, 0))
            { TitanFtp_CloseFileTransfer(1); TitanFtp_CloseData(); (void)TitanFtp_Send("451 Publish failed.\r\n"); return; }
            TitanFtp_CloseFileTransfer(0); TitanFtp_CloseData(); (void)TitanFtp_Send("226 File upload committed.\r\n"); return;
        }
        TitanFtp_CloseFileTransfer(1); TitanFtp_CloseData(); (void)TitanFtp_Send("426 Upload aborted.\r\n");
    }
}

static int TitanFtp_RootAllowed(const char *root)
{
    if (root == 0) return 0;
    return strncmp(root, "mass:/", 6u) == 0 ||
        strncmp(root, "mass0:/", 7u) == 0 || strncmp(root, "mass1:/", 7u) == 0 ||
        strncmp(root, "mass2:/", 7u) == 0 || strncmp(root, "mass3:/", 7u) == 0 ||
        strncmp(root, "mass4:/", 7u) == 0 || strncmp(root, "mass5:/", 7u) == 0 ||
        strncmp(root, "mass6:/", 7u) == 0 || strncmp(root, "mass7:/", 7u) == 0 ||
        strncmp(root, "mass8:/", 7u) == 0 || strncmp(root, "mass9:/", 7u) == 0 ||
        strncmp(root, "mc0:/", 5u) == 0 || strncmp(root, "mc1:/", 5u) == 0;
}

static int TitanFtp_PathComponentSafe(const char *path)
{
    const char *p = path;
    if (path == 0) return 0;
    while (*p != '\0')
    {
        const char *start;
        unsigned int length = 0u;
        while (*p == '/') p++;
        start = p;
        while (*p != '\0' && *p != '/') { p++; length++; }
        if (length == 2u && start[0] == '.' && start[1] == '.') return 0;
    }
    return 1;
}

static int TitanFtp_JoinPath(const char *argument, char *output, unsigned int capacity)
{
    int count;
    if (argument == 0 || output == 0 || capacity == 0u || !TitanFtp_PathComponentSafe(argument)) return 0;
    if (argument[0] == '/')
        count = snprintf(output, capacity, "%s%s", g_snapshot.root, argument + 1);
    else if (strcmp(g_snapshot.cwd, "/") == 0)
        count = snprintf(output, capacity, "%s%s", g_snapshot.root, argument);
    else
        count = snprintf(output, capacity, "%s%s/%s", g_snapshot.root, g_snapshot.cwd + 1, argument);
    return count > 0 && (unsigned int)count < capacity;
}

static int TitanFtp_Send(const char *text)
{
    unsigned int sent = 0u;
    if (g_control == TITAN_NETWORK_STREAM_INVALID_HANDLE) return 0;
    return TitanNetworkStream_Write(g_control, text, (unsigned int)strlen(text), 1000u, &sent) &&
        sent == strlen(text);
}

static void TitanFtp_CloseControl(void)
{
    if (g_control != TITAN_NETWORK_STREAM_INVALID_HANDLE)
        TitanNetworkStream_Close(g_control);
    g_control = TITAN_NETWORK_STREAM_INVALID_HANDLE;
    g_control_used = 0u;
    g_logged_in = 0;
    if (g_snapshot.state != TITAN_FTP_STATE_STOPPED) g_snapshot.state = TITAN_FTP_STATE_LISTENING;
}

static void TitanFtp_Upper(char *text)
{
    while (*text != '\0') { *text = (char)toupper((unsigned char)*text); text++; }
}

static void TitanFtp_ProcessLine(char *line)
{
    char *argument;
    char command[8];
    unsigned int i = 0u;
    int is_directory = 0;
    TitanStorageIoSize size = 0u;
    char path[TITAN_FTP_PATH_CAPACITY * 2u];
    char reply[196];

    while (*line == ' ' || *line == '\t') line++;
    while (*line != '\0' && *line != ' ' && *line != '\t' && i + 1u < sizeof(command)) command[i++] = *line++;
    command[i] = '\0';
    TitanFtp_Upper(command);
    while (*line == ' ' || *line == '\t') line++;
    argument = line;
    g_snapshot.commands_processed++;

    if (strcmp(command, "USER") == 0)
    {
        (void)TitanFtp_Send("331 Anonymous login accepted; send PASS.\r\n");
        return;
    }
    if (strcmp(command, "PASS") == 0)
    {
        g_logged_in = 1;
        (void)TitanFtp_Send("230 Project Titan FTP login successful.\r\n");
        return;
    }
    if (strcmp(command, "QUIT") == 0)
    {
        (void)TitanFtp_Send("221 Goodbye.\r\n");
        TitanFtp_CloseControl();
        return;
    }
    if (strcmp(command, "NOOP") == 0)
    {
        (void)TitanFtp_Send("200 NOOP ok.\r\n");
        return;
    }
    if (strcmp(command, "SYST") == 0)
    {
        (void)TitanFtp_Send("215 UNIX Type: L8; Project Titan VFS\r\n");
        return;
    }
    if (!g_logged_in)
    {
        g_snapshot.commands_rejected++;
        (void)TitanFtp_Send("530 Login with USER/PASS first.\r\n");
        return;
    }
    if (strcmp(command, "TYPE") == 0)
    {
        if (argument[0] == 'I' || argument[0] == 'i' || argument[0] == 'A' || argument[0] == 'a')
            (void)TitanFtp_Send("200 Type set.\r\n");
        else { g_snapshot.commands_rejected++; (void)TitanFtp_Send("504 Type not supported.\r\n"); }
        return;
    }
    if (strcmp(command, "PWD") == 0)
    {
        (void)snprintf(reply, sizeof(reply), "257 \"%s\" is current directory.\r\n", g_snapshot.cwd);
        (void)TitanFtp_Send(reply);
        return;
    }
    if (strcmp(command, "CWD") == 0)
    {
        if (argument[0] == '\0' || !TitanFtp_JoinPath(argument, path, sizeof(path)) ||
            !TitanStorageIo_PathInfo64(path, &is_directory, &size) || !is_directory)
        {
            g_snapshot.commands_rejected++;
            (void)TitanFtp_Send("550 Directory unavailable.\r\n");
            return;
        }
        if (argument[0] == '/') (void)snprintf(g_snapshot.cwd, sizeof(g_snapshot.cwd), "%s", argument);
        else if (strcmp(g_snapshot.cwd, "/") == 0)
            (void)snprintf(g_snapshot.cwd, sizeof(g_snapshot.cwd), "/%s", argument);
        else
        {
            size_t old_length = strlen(g_snapshot.cwd);
            size_t argument_length = strlen(argument);
            if (old_length + 1u + argument_length + 1u > sizeof(g_snapshot.cwd))
            {
                g_snapshot.commands_rejected++;
                (void)TitanFtp_Send("550 Path too long.\r\n");
                return;
            }
            g_snapshot.cwd[old_length] = '/';
            memcpy(g_snapshot.cwd + old_length + 1u, argument, argument_length + 1u);
        }
        (void)TitanFtp_Send("250 Directory changed.\r\n");
        return;
    }
    if (strcmp(command, "EPSV") == 0)
    {
        if (!TitanFtp_OpenPassive()) { g_snapshot.commands_rejected++; (void)TitanFtp_Send("425 Cannot open passive listener.\r\n"); return; }
        (void)snprintf(reply, sizeof(reply), "229 Entering Extended Passive Mode (|||%u|).\r\n", (unsigned int)g_passive_port);
        (void)TitanFtp_Send(reply);
        return;
    }
    if (strcmp(command, "PASV") == 0)
    {
        unsigned int octets[4];
        const TitanNetworkStatus *network = TitanNetwork_GetStatus();
        if (network == 0 || !TitanFtp_ParseIpv4(network->ip, octets) || !TitanFtp_OpenPassive())
        { g_snapshot.commands_rejected++; (void)TitanFtp_Send("425 Cannot open passive listener.\r\n"); return; }
        (void)snprintf(reply, sizeof(reply), "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).\r\n",
            octets[0],octets[1],octets[2],octets[3],(unsigned int)(g_passive_port>>8),(unsigned int)(g_passive_port&255u));
        (void)TitanFtp_Send(reply);
        return;
    }
    if (strcmp(command, "LIST") == 0 || strcmp(command, "NLST") == 0)
    {
        if (g_data_listener == TITAN_NETWORK_STREAM_INVALID_HANDLE || g_list_pending)
        { g_snapshot.commands_rejected++; (void)TitanFtp_Send("425 Use PASV or EPSV first.\r\n"); return; }
        if (argument[0] != '\0')
        { if (!TitanFtp_JoinPath(argument, g_list_path, sizeof(g_list_path))) { g_snapshot.commands_rejected++; (void)TitanFtp_Send("550 Invalid path.\r\n"); return; } }
        else if (!TitanFtp_JoinPath("", g_list_path, sizeof(g_list_path)))
        { g_snapshot.commands_rejected++; (void)TitanFtp_Send("550 Invalid path.\r\n"); return; }
        g_list_index=0u; g_list_pending=1; g_list_names_only=(strcmp(command,"NLST")==0);
        (void)TitanFtp_Send("150 Opening passive data connection.\r\n");
        return;
    }
    if (strcmp(command, "RETR") == 0)
    {
        if (g_data_listener == TITAN_NETWORK_STREAM_INVALID_HANDLE || argument[0] == '\0' || !TitanFtp_JoinPath(argument, path, sizeof(path)))
        { g_snapshot.commands_rejected++; (void)TitanFtp_Send("425 Use passive mode and a valid path first.\r\n"); return; }
        g_file_fd = TitanStorageIo_Open(path, TITAN_STORAGE_IO_READ);
        if (g_file_fd < 0) { g_snapshot.commands_rejected++; (void)TitanFtp_Send("550 File unavailable.\r\n"); return; }
        g_file_mode = 1; g_file_temp[0]='\0'; (void)snprintf(g_file_target,sizeof(g_file_target),"%s",path);
        (void)TitanFtp_Send("150 Opening file download.\r\n"); return;
    }
    if (strcmp(command, "STOR") == 0)
    {
        int n;
        if (g_data_listener == TITAN_NETWORK_STREAM_INVALID_HANDLE || argument[0] == '\0' || !TitanFtp_JoinPath(argument, path, sizeof(path)))
        { g_snapshot.commands_rejected++; (void)TitanFtp_Send("425 Use passive mode and a valid path first.\r\n"); return; }
        n=snprintf(g_file_temp,sizeof(g_file_temp),"%s.titantmp",path);
        if (n<=0 || (unsigned int)n>=sizeof(g_file_temp)) { g_snapshot.commands_rejected++; (void)TitanFtp_Send("550 Path too long.\r\n"); return; }
        g_file_fd = TitanStorageIo_Open(g_file_temp, TITAN_STORAGE_IO_WRITE|TITAN_STORAGE_IO_CREATE|TITAN_STORAGE_IO_TRUNCATE);
        if (g_file_fd < 0) { g_snapshot.commands_rejected++; (void)TitanFtp_Send("550 Upload target unavailable.\r\n"); return; }
        g_file_mode = 2; (void)snprintf(g_file_target,sizeof(g_file_target),"%s",path);
        (void)TitanFtp_Send("150 Opening transactional upload.\r\n"); return;
    }
    g_snapshot.commands_rejected++;
    (void)TitanFtp_Send("502 Command not implemented.\r\n");
}

int TitanFtpService_Start(const char *root, unsigned short port)
{
    if (g_snapshot.state != TITAN_FTP_STATE_STOPPED || !TitanFtp_RootAllowed(root)) return 0;
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    if (strlen(root) >= sizeof(g_snapshot.root)) return 0;
    (void)snprintf(g_snapshot.root, sizeof(g_snapshot.root), "%s", root);
    (void)snprintf(g_snapshot.cwd, sizeof(g_snapshot.cwd), "/");
    g_snapshot.control_port = port != 0u ? port : TITAN_FTP_DEFAULT_PORT;
    if (!TitanNetworkStream_Listen(TITAN_NETWORK_STREAM_OWNER_FTP, g_snapshot.control_port, 2u, &g_listener))
    {
        memset(&g_snapshot, 0, sizeof(g_snapshot));
        return 0;
    }
    g_control = TITAN_NETWORK_STREAM_INVALID_HANDLE;
    g_data_listener = TITAN_NETWORK_STREAM_INVALID_HANDLE;
    g_data_stream = TITAN_NETWORK_STREAM_INVALID_HANDLE;
    g_passive_port = TITAN_FTP_PASSIVE_PORT_BASE;
    g_snapshot.state = TITAN_FTP_STATE_LISTENING;
    return 1;
}

void TitanFtpService_Tick(void)
{
    if (g_snapshot.state == TITAN_FTP_STATE_STOPPED) return;
    if (g_file_mode != 0) { TitanFtp_TickFile(); return; }
    if (g_list_pending) { TitanFtp_TickList(); return; }
    if (g_control == TITAN_NETWORK_STREAM_INVALID_HANDLE)
    {
        if (TitanNetworkStream_Accept(g_listener, TITAN_FTP_TICK_DEADLINE_MS, &g_control))
        {
            g_snapshot.state = TITAN_FTP_STATE_CONNECTED;
            g_snapshot.sessions_accepted++;
            g_control_used = 0u;
            g_logged_in = 0;
            if (!TitanFtp_Send("220 Project Titan FTP service ready.\r\n")) TitanFtp_CloseControl();
        }
        return;
    }
    if (g_control_used + 1u < sizeof(g_control_buffer))
    {
        unsigned int received = 0u;
        if (TitanNetworkStream_Read(g_control, g_control_buffer + g_control_used,
            (unsigned int)sizeof(g_control_buffer) - 1u - g_control_used,
            TITAN_FTP_TICK_DEADLINE_MS, &received))
        {
            char *newline;
            g_control_used += received;
            g_control_buffer[g_control_used] = '\0';
            newline = strchr(g_control_buffer, '\n');
            if (newline != 0)
            {
                unsigned int consumed;
                char *end = newline;
                *newline = '\0';
                if (end > g_control_buffer && end[-1] == '\r') end[-1] = '\0';
                TitanFtp_ProcessLine(g_control_buffer);
                consumed = (unsigned int)(newline - g_control_buffer) + 1u;
                if (consumed < g_control_used)
                    memmove(g_control_buffer, g_control_buffer + consumed, g_control_used - consumed);
                g_control_used -= consumed;
            }
        }
    }
    else
    {
        g_snapshot.protocol_errors++;
        (void)TitanFtp_Send("500 Command line too long.\r\n");
        TitanFtp_CloseControl();
    }
}

void TitanFtpService_Stop(void)
{
    TitanNetworkStream_CancelOwner(TITAN_NETWORK_STREAM_OWNER_FTP);
    TitanFtp_CloseControl();
    TitanFtp_CloseFileTransfer(1);
    TitanFtp_CloseData();
    if (g_listener != TITAN_NETWORK_STREAM_INVALID_HANDLE) TitanNetworkStream_Close(g_listener);
    g_listener = TITAN_NETWORK_STREAM_INVALID_HANDLE;
    g_control = TITAN_NETWORK_STREAM_INVALID_HANDLE;
    memset(&g_snapshot, 0, sizeof(g_snapshot));
}

void TitanFtpService_GetSnapshot(TitanFtpServiceSnapshot *snapshot)
{
    if (snapshot != 0) *snapshot = g_snapshot;
}
