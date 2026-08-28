#ifndef TITAN_HOST_STUB_FILEXIO_RPC_H
#define TITAN_HOST_STUB_FILEXIO_RPC_H

#ifndef NEWLIB_PORT_AWARE
#error "Using fio/fileXio functions directly in the newlib port will lead to problems."
#error "Use posix function calls instead."
#endif

#define FXIO_WAIT 0
#define FXIO_NOWAIT 1
#define FXIO_COMPLETE 1
#define FXIO_INCOMPLETE 0

int fileXioInit(void);
int fileXioExit(void);
void fileXioSetBlockMode(int mode);
int fileXioWaitAsync(int mode, int *retVal);
int fileXioOpen(const char *name, int flags, ...);
int fileXioClose(int fd);
int fileXioRead(int fd, void *buffer, int size);
int fileXioWrite(int fd, const void *buffer, int size);
int fileXioRemove(const char *name);
int fileXioRename(const char *source, const char *destination);

#endif
