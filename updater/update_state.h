#ifndef UPDATER_UPDATE_STATE_H
#define UPDATER_UPDATE_STATE_H

#include <stddef.h>

#define UPDATER_PATH_CAP 4096

typedef struct {
    char phase[64];
    char target[UPDATER_PATH_CAP];
    char staging[UPDATER_PATH_CAP];
    char backup[UPDATER_PATH_CAP];
    char package[UPDATER_PATH_CAP];
} updater_state_t;

enum {
    UPDATER_RESUME_FULL = 0,
    UPDATER_RESUME_POST_EXTRACT,
    UPDATER_RESUME_POST_BACKUP,
    UPDATER_RESUME_POST_INSTALL,
    UPDATER_RESUME_ROLLBACK
};

int updater_state_build_path(char *out, size_t cap, const char *install_dir);

int updater_state_load(const char *state_path, updater_state_t *st, int *out_found);

/** Returns 0 if state matches this run (paths + package), else non-zero. */
int updater_state_validate(const updater_state_t *st,
    const char *install_dir,
    const char *staging,
    const char *backup,
    const char *zip_path);

/**
 * Uses filesystem layout under install_dir / staging / backup.
 * out_found from load: if no valid state, pass *out_found == 0 and NULL st (or ignore st).
 */
int updater_determine_resume(const char *install_dir,
    const char *staging,
    const char *backup,
    const updater_state_t *st,
    int state_found,
    int *out_resume);

int updater_state_save_atomic(const char *state_path,
    const char *phase,
    const char *install_dir,
    const char *staging,
    const char *backup,
    const char *package);

void updater_state_remove(const char *state_path);

#endif /* UPDATER_UPDATE_STATE_H */
