#ifndef PLATFORM_PROCESS_H
#define PLATFORM_PROCESS_H

#include "platform_errors.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * argv-style argument list, NULL-terminated. argv[0] is conventionally the program name.
 * Returns PLATFORM_OK and sets *out_pid on success.
 */
int platform_process_spawn(const char *path, const char *const *argv, int *out_pid);

int platform_process_get_current_pid(void);

/**
 * Waits until the process identified by pid exits.
 * If out_exit_code is non-NULL, stores the process exit status (0–255 where available).
 */
int platform_process_wait_for_pid_exit(int pid, int *out_exit_code);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_PROCESS_H */
