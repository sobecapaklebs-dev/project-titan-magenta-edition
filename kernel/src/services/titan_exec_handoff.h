#ifndef PROJECT_TITAN_EXEC_HANDOFF_H
#define PROJECT_TITAN_EXEC_HANDOFF_H

typedef enum TitanExecHandoffResult
{
    TITAN_EXEC_HANDOFF_OK = 0,
    TITAN_EXEC_HANDOFF_INVALID_ARGUMENT = -1,
    TITAN_EXEC_HANDOFF_INVALID_STAGE2 = -2,
    TITAN_EXEC_HANDOFF_STAGE2_RANGE = -3,
    TITAN_EXEC_HANDOFF_EXEC_RETURNED = -4,
    TITAN_EXEC_HANDOFF_PATCH_ARGUMENT = -5
} TitanExecHandoffResult;

TitanExecHandoffResult TitanExecHandoff_Run(
    int argc,
    char *argv[]
);

/* Installs the optional low-memory PS2RD segment from the already-embedded
 * handoff stage2, then forwards the caller's argv to the target unchanged.
 * This is intentionally sideband-only: Golden engine argv never contains the
 * Titan internal marker. */
TitanExecHandoffResult TitanExecHandoff_RunWithPatch(
    int argc,
    char *argv[],
    const char *patch_payload_path
);

#endif
