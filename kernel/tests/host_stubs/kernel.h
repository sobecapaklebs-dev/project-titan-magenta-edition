#ifndef TITAN_HOST_STUB_KERNEL_H
#define TITAN_HOST_STUB_KERNEL_H

#define WRITEBACK_DCACHE 0
#define INVALIDATE_ICACHE 2
#define EE_SYNCP() ((void)0)

int LoadExecPS2(const char *path, int argc, char *argv[]);
int ExecPS2(void *entry, void *gp, int argc, char *argv[]);
void FlushCache(int operation);

typedef struct ee_thread_t
{
    int attr;
    int option;
    void (*func)(void *);
    void *stack;
    int stack_size;
    void *gp_reg;
    int initial_priority;
} ee_thread_t;

typedef struct ee_sema_t
{
    int attr;
    int option;
    int init_count;
    int max_count;
} ee_sema_t;

int CreateThread(ee_thread_t *thread);
int StartThread(int thread_id, void *argument);
int DeleteThread(int thread_id);
int TerminateThread(int thread_id);
void ExitThread(void);
void SleepThread(void);
int GetThreadId(void);
int CreateSema(ee_sema_t *sema);
int DeleteSema(int sema_id);
int WaitSema(int sema_id);
int SignalSema(int sema_id);

#endif
