/**
 * savedir.h — Smart save directory detection for the GE007 PC port.
 *
 * Determines where to store persistent files (eeprom, config) using:
 *   1. --savedir <path> command-line override
 *   2. Current working directory (if writable and config already exists)
 *   3. $HOME/.ge007/ (created on demand)
 */
#ifndef _PLATFORM_SAVEDIR_H_
#define _PLATFORM_SAVEDIR_H_

/**
 * Initialize the save directory path.
 * Call once during startup, after command-line parsing.
 * If savedir_override is non-NULL, it is used directly.
 * Returns 0 on success; -1 only when an EXPLICIT override is unusable
 * (uncreatable / not writable) — the caller must surface that (AUDIT-0054).
 * The auto-selection paths always return 0. Idempotent after the first call.
 */
int savedirInit(const char *savedir_override);

/**
 * Build a full path to a file in the save directory.
 * Returns a pointer to a static buffer (not thread-safe).
 * Example: savedirPath("ge007_eeprom.bin") → "/home/user/.ge007/ge007_eeprom.bin"
 */
const char *savedirPath(const char *filename);

/**
 * Return the resolved save directory (with trailing slash).
 */
const char *savedirGet(void);

#endif /* _PLATFORM_SAVEDIR_H_ */
