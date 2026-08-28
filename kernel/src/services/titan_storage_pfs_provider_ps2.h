#ifndef PROJECT_TITAN_STORAGE_PFS_PROVIDER_PS2_H
#define PROJECT_TITAN_STORAGE_PFS_PROVIDER_PS2_H

/* Registers a platform PFS provider only when the three optional platform
 * hooks are linked into the kernel. Returns 1 when registered, 0 when the
 * backend is absent or unavailable. */
int TitanStoragePfsProviderPs2_RegisterIfAvailable(void);

#endif
