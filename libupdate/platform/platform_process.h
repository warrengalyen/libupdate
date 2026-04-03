#ifndef PLATFORM_PROCESS_H
#define PLATFORM_PROCESS_H

#include "platform_errors.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Spawn process with NULL-terminated argv. Sets *out_pid on success. */
int platform_process_spawn(const char *path, const char *const *argv, int *out_pid);

int platform_process_get_current_pid(void);

/** Block until pid exits. Optionally stores exit status in *out_exit_code. */
int platform_process_wait_for_pid_exit(int pid, int *out_exit_code);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_PROCESS_H */
