#ifndef PLATFORM_FS_H
#define PLATFORM_FS_H

#include "platform_errors.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int platform_fs_get_executable_path(char *out, size_t out_size);
int platform_fs_get_executable_dir(char *out, size_t out_size);

/** Writes system temp directory (no trailing separator). */
int platform_fs_get_system_temp_dir(char *out, size_t out_size);

int platform_fs_create_directory_recursive(const char *path);
int platform_fs_move_path(const char *src, const char *dst);
int platform_fs_remove_path(const char *path);

/** POSIX: chmod path to (mode & 07777). Windows: no-op success. */
int platform_fs_chmod(const char *path, unsigned mode);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_FS_H */
