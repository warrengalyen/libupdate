#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <update.h>

#include "update_state.h"

#define PATH_CAP UPDATER_PATH_CAP

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s --install <zip_path> --target <install_dir> --pid <parent_pid> --app <app_name>\n",
        prog);
}

static int streq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

static int is_absolute_app_path(const char *app)
{
    if (app == NULL || app[0] == '\0') {
        return 0;
    }

#if defined(_WIN32) && !defined(__CYGWIN__)
    if (app[0] != '\0' && app[1] == ':') {
        return 1;
    }
    if (app[0] == '\\' && app[1] == '\\') {
        return 1;
    }
#endif
    return app[0] == '/';
}

static int join_install_app(const char *install_dir, const char *app, char *out, size_t cap)
{
    int n;

    if (is_absolute_app_path(app) != 0) {
        n = snprintf(out, cap, "%s", app);
    } else {
        n = snprintf(out, cap, "%s/%s", install_dir, app);
    }

    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return 0;
}

static int parse_args(int argc, char **argv,
    const char **out_zip,
    const char **out_target,
    int *out_pid,
    const char **out_app)
{
    int i;
    const char *zip = NULL;
    const char *target = NULL;
    const char *app = NULL;
    int pid = -1;

    for (i = 1; i < argc; i++) {
        if (streq(argv[i], "--install")) {
            if (i + 1 >= argc) {
                return -1;
            }
            zip = argv[++i];
            continue;
        }
        if (streq(argv[i], "--target")) {
            if (i + 1 >= argc) {
                return -1;
            }
            target = argv[++i];
            continue;
        }
        if (streq(argv[i], "--pid")) {
            if (i + 1 >= argc) {
                return -1;
            }
            pid = atoi(argv[++i]);
            continue;
        }
        if (streq(argv[i], "--app")) {
            if (i + 1 >= argc) {
                return -1;
            }
            app = argv[++i];
            continue;
        }
        return -1;
    }

    if (zip == NULL || zip[0] == '\0' || target == NULL || target[0] == '\0' || app == NULL
        || app[0] == '\0' || pid <= 0) {
        return -1;
    }

    *out_zip = zip;
    *out_target = target;
    *out_pid = pid;
    *out_app = app;
    return 0;
}

int main(int argc, char **argv)
{
    const char *zip_path;
    const char *install_dir;
    const char *app_arg;
    int parent_pid;
    char staging[PATH_CAP];
    char backup[PATH_CAP];
    char state_path[PATH_CAP];
    char app_path[PATH_CAP];
    updater_state_t st;
    int state_found = 0;
    int resume = UPDATER_RESUME_FULL;
    int n;
    int ld;

    if (parse_args(argc, argv, &zip_path, &install_dir, &parent_pid, &app_arg) != 0) {
        usage(stderr, argv[0] != NULL ? argv[0] : "updater");
        return (int)UPDATE_ERROR;
    }

    n = snprintf(staging, sizeof staging, "%s.update_staging", install_dir);
    if (n < 0 || (size_t)n >= sizeof staging) {
        return (int)UPDATE_ERROR;
    }

    n = snprintf(backup, sizeof backup, "%s.update_backup", install_dir);
    if (n < 0 || (size_t)n >= sizeof backup) {
        return (int)UPDATE_ERROR;
    }

    if (updater_state_build_path(state_path, sizeof state_path, install_dir) != 0) {
        return (int)UPDATE_ERROR;
    }

    ld = updater_state_load(state_path, &st, &state_found);
    if (ld != 0) {
        updater_state_remove(state_path);
        state_found = 0;
    } else if (state_found != 0) {
        if (updater_state_validate(&st, install_dir, staging, backup, zip_path) != 0) {
            updater_state_remove(state_path);
            state_found = 0;
        } else if (updater_determine_resume(install_dir, staging, backup, &st, state_found, &resume) != 0) {
            return (int)UPDATE_ERROR;
        }
    }

    if (update_wait_for_parent_exit(parent_pid) != UPDATE_OK) {
        return (int)UPDATE_ERROR;
    }

    if (join_install_app(install_dir, app_arg, app_path, sizeof app_path) != 0) {
        return (int)UPDATE_ERROR;
    }

    if (resume == UPDATER_RESUME_ROLLBACK) {
        if (update_move_path(backup, install_dir) != UPDATE_OK) {
            return (int)UPDATE_ERROR;
        }
        (void)update_remove_tree(staging);
        updater_state_remove(state_path);
        if (update_relaunch_app(app_path) != UPDATE_OK) {
            return (int)UPDATE_ERROR;
        }
        return (int)UPDATE_OK;
    }

    if (resume == UPDATER_RESUME_POST_INSTALL) {
        (void)update_remove_tree(backup);
        updater_state_remove(state_path);
        if (update_relaunch_app(app_path) != UPDATE_OK) {
            return (int)UPDATE_ERROR;
        }
        return (int)UPDATE_OK;
    }

    if (resume == UPDATER_RESUME_POST_BACKUP) {
        if (update_move_path(staging, install_dir) != UPDATE_OK) {
            if (update_move_path(backup, install_dir) != UPDATE_OK) {
                return (int)UPDATE_ERROR;
            }
            (void)update_remove_tree(staging);
            updater_state_remove(state_path);
            return (int)UPDATE_ERROR;
        }
        (void)updater_state_save_atomic(state_path, "installed", install_dir, staging, backup, zip_path);
        (void)update_remove_tree(backup);
        updater_state_remove(state_path);
        if (update_relaunch_app(app_path) != UPDATE_OK) {
            return (int)UPDATE_ERROR;
        }
        return (int)UPDATE_OK;
    }

    /* UPDATER_RESUME_FULL or UPDATER_RESUME_POST_EXTRACT */
    if (resume == UPDATER_RESUME_FULL) {
        (void)update_remove_tree(staging);
        if (update_extract(zip_path, staging) != UPDATE_OK) {
            (void)update_remove_tree(staging);
            updater_state_remove(state_path);
            return (int)UPDATE_ERROR;
        }
    }

    if (updater_state_save_atomic(state_path, "extracted", install_dir, staging, backup, zip_path) != UPDATE_OK) {
        (void)update_remove_tree(staging);
        updater_state_remove(state_path);
        return (int)UPDATE_ERROR;
    }

    (void)update_remove_tree(backup);

    if (update_move_path(install_dir, backup) != UPDATE_OK) {
        (void)update_remove_tree(staging);
        updater_state_remove(state_path);
        return (int)UPDATE_ERROR;
    }

    if (updater_state_save_atomic(state_path, "install_backed_up", install_dir, staging, backup, zip_path)
        != UPDATE_OK) {
        if (update_move_path(backup, install_dir) != UPDATE_OK) {
            return (int)UPDATE_ERROR;
        }
        (void)update_remove_tree(staging);
        updater_state_remove(state_path);
        return (int)UPDATE_ERROR;
    }

    if (update_move_path(staging, install_dir) != UPDATE_OK) {
        if (update_move_path(backup, install_dir) != UPDATE_OK) {
            return (int)UPDATE_ERROR;
        }
        (void)update_remove_tree(staging);
        updater_state_remove(state_path);
        return (int)UPDATE_ERROR;
    }

    (void)updater_state_save_atomic(state_path, "installed", install_dir, staging, backup, zip_path);

    (void)update_remove_tree(backup);
    updater_state_remove(state_path);

    if (update_relaunch_app(app_path) != UPDATE_OK) {
        return (int)UPDATE_ERROR;
    }

    return (int)UPDATE_OK;
}
