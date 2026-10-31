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

/* IOP FileXio open-flag namespace used by direct async storage tests. */
#define FIO_O_RDONLY 0x0001
#define FIO_O_WRONLY 0x0002
#define FIO_O_RDWR   0x0003
#define FIO_O_APPEND 0x0008
#define FIO_O_CREAT  0x0200
#define FIO_O_TRUNC  0x0400
#define FIO_O_EXCL   0x0800

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
