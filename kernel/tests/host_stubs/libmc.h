#ifndef TITAN_HOST_STUB_LIBMC_H
#define TITAN_HOST_STUB_LIBMC_H

#define MC_TYPE_PS2 2
#define MC_TYPE_MC 0
#define MC_TYPE_XMC 1
#define MC_WAIT 0
#define MC_NOWAIT 1

typedef struct sceMcTblGetDir {
    unsigned char AttrFile;
    unsigned char Reserve1[3];
    unsigned int FileSizeByte;
    unsigned char Reserve2[20];
    unsigned char EntryName[32];
} sceMcTblGetDir;

int mcInit(int type);
int mcGetInfo(int port, int slot, int *type, int *free_space, int *formatted);
int mcFormat(int port, int slot);
int mcSync(int mode, int *command, int *result);
int mcGetDir(int port, int slot, const char *name, unsigned int mode, int maxent, sceMcTblGetDir *table);
int mcOpen(int port, int slot, const char *name, int mode);
int mcClose(int fd);
int mcRead(int fd, void *buffer, int size);
int mcWrite(int fd, const void *buffer, int size);
int mcDelete(int port, int slot, const char *name);
int mcRename(int port, int slot, const char *oldName, const char *newName);

#endif
