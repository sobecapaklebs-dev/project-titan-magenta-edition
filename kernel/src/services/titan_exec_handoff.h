#ifndef PROJECT_TITAN_EXEC_HANDOFF_H
#define PROJECT_TITAN_EXEC_HANDOFF_H

typedef enum TitanExecHandoffResult
{
    TITAN_EXEC_HANDOFF_OK = 0,
    TITAN_EXEC_HANDOFF_INVALID_ARGUMENT = -1,
    TITAN_EXEC_HANDOFF_INVALID_STAGE2 = -2,
    TITAN_EXEC_HANDOFF_STAGE2_RANGE = -3,
    TITAN_EXEC_HANDOFF_EXEC_RETURNED = -4
} TitanExecHandoffResult;

TitanExecHandoffResult TitanExecHandoff_Run(
    int argc,
    char *argv[]
);

#endif
