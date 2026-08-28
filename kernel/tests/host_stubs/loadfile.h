#ifndef TITAN_HOST_STUB_LOADFILE_H
#define TITAN_HOST_STUB_LOADFILE_H

typedef struct t_ExecData
{
    unsigned int epc;
    unsigned int gp;
    unsigned int sp;
    unsigned int dummy;
} t_ExecData;

int SifExecModuleBuffer(
    void *module,
    int module_size,
    int argument_length,
    const char *arguments,
    int *module_result
);
int SifLoadFileInit(void);
int SifLoadFileExit(void);
int SifLoadModule(
    const char *path,
    int argument_length,
    const char *arguments
);
int SifLoadElf(const char *path, t_ExecData *data);

#endif
