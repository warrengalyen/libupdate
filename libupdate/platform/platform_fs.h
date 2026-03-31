#ifndef PLATFORM_FS_H
#define PLATFORM_FS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Filesystem abstraction (declarations only; implementations are stubs).
 */
int platform_fs_exists_stub(void);
int platform_fs_remove_stub(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_FS_H */
