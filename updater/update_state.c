#include "update_state.h"

#include <update.h>
#include "json_mini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__CYGWIN__)
    #include <windows.h>
#else
    #include <sys/stat.h>
#endif

#define STATE_READ_MAX (65536U)

static int path_is_dir(const char *path)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    return (a & FILE_ATTRIBUTE_DIRECTORY) != 0U ? 1 : 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

int updater_state_build_path(char *out, size_t cap, const char *install_dir)
{
    int n;

    if (out == NULL || cap == 0U || install_dir == NULL || install_dir[0] == '\0') {
        return -1;
    }

    n = snprintf(out, cap, "%s.update_state.json", install_dir);
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }

    return 0;
}

int updater_state_load(const char *state_path, updater_state_t *st, int *out_found)
{
    FILE *fp;
    char *buf = NULL;
    size_t cap;
    size_t len;
    size_t nread;

    if (state_path == NULL || state_path[0] == '\0' || st == NULL || out_found == NULL) {
        return -1;
    }

    *out_found = 0;
    memset(st, 0, sizeof(*st));

    fp = fopen(state_path, "rb");
    if (fp == NULL) {
        return 0;
    }

    cap = 4096U;
    buf = (char *)malloc(cap);
    if (buf == NULL) {
        fclose(fp);
        return -1;
    }

    len = 0U;
    for (;;) {
        if (len + 4096U > STATE_READ_MAX) {
            free(buf);
            fclose(fp);
            return -1;
        }
        if (len + 4096U > cap) {
            char *nb = (char *)realloc(buf, cap + 4096U);
            if (nb == NULL) {
                free(buf);
                fclose(fp);
                return -1;
            }
            buf = nb;
            cap += 4096U;
        }
        nread = fread(buf + len, 1U, 4096U, fp);
        len += nread;
        buf[len] = '\0';
        if (nread < 4096U) {
            break;
        }
    }

    fclose(fp);

    if (json_mini_extract_string(buf, "phase", st->phase, sizeof st->phase) != 0
        || json_mini_extract_string(buf, "target", st->target, sizeof st->target) != 0
        || json_mini_extract_string(buf, "staging", st->staging, sizeof st->staging) != 0
        || json_mini_extract_string(buf, "backup", st->backup, sizeof st->backup) != 0
        || json_mini_extract_string(buf, "package", st->package, sizeof st->package) != 0) {
        free(buf);
        return -1;
    }

    free(buf);
    *out_found = 1;
    return 0;
}

int updater_state_validate(const updater_state_t *st,
    const char *install_dir,
    const char *staging,
    const char *backup,
    const char *zip_path)
{
    if (st == NULL || install_dir == NULL || staging == NULL || backup == NULL || zip_path == NULL) {
        return -1;
    }

    if (strcmp(st->target, install_dir) != 0) {
        return -1;
    }

    if (strcmp(st->staging, staging) != 0 || strcmp(st->backup, backup) != 0) {
        return -1;
    }

    if (strcmp(st->package, zip_path) != 0) {
        return -1;
    }

    if (strcmp(st->staging, install_dir) == 0) {
        return -1;
    }

    if (update_validate_path(st->target, UPDATE_PATH_REQUIRE_ABSOLUTE) != UPDATE_OK
        || update_validate_path(st->staging, UPDATE_PATH_REQUIRE_ABSOLUTE) != UPDATE_OK
        || update_validate_path(st->backup, UPDATE_PATH_REQUIRE_ABSOLUTE) != UPDATE_OK
        || update_validate_path(st->package, 0U) != UPDATE_OK) {
        return -1;
    }

    return 0;
}

int updater_determine_resume(const char *install_dir,
    const char *staging,
    const char *backup,
    const updater_state_t *st,
    int state_found,
    int *out_resume)
{
    int inst;
    int stg;
    int bak;

    if (install_dir == NULL || staging == NULL || backup == NULL || out_resume == NULL) {
        return -1;
    }

    if (!state_found || st == NULL) {
        *out_resume = UPDATER_RESUME_FULL;
        return 0;
    }

    inst = path_is_dir(install_dir);
    stg = path_is_dir(staging);
    bak = path_is_dir(backup);

    if (inst == 0 && bak != 0 && stg == 0) {
        *out_resume = UPDATER_RESUME_ROLLBACK;
        return 0;
    }

    if (inst == 0 && bak != 0 && stg != 0) {
        *out_resume = UPDATER_RESUME_POST_BACKUP;
        return 0;
    }

    if (inst != 0 && stg == 0 && bak != 0) {
        *out_resume = UPDATER_RESUME_POST_INSTALL;
        return 0;
    }

    if (inst != 0 && stg != 0 && bak == 0) {
        if (strcmp(st->phase, "extracted") == 0) {
            *out_resume = UPDATER_RESUME_POST_EXTRACT;
            return 0;
        }
        *out_resume = UPDATER_RESUME_FULL;
        return 0;
    }

    if (inst != 0 && stg != 0 && bak != 0) {
        if (strcmp(st->phase, "extracted") == 0) {
            *out_resume = UPDATER_RESUME_POST_EXTRACT;
            return 0;
        }
        *out_resume = UPDATER_RESUME_FULL;
        return 0;
    }

    *out_resume = UPDATER_RESUME_FULL;
    return 0;
}

static void json_emit_escaped_string(FILE *fp, const char *s)
{
    fputc((int)'"', fp);
    for (; s != NULL && *s != '\0'; s++) {
        if (*s == '"' || *s == '\\') {
            fputc((int)'\\', fp);
        }
        fputc((int)(unsigned char)*s, fp);
    }
    fputc((int)'"', fp);
}

int updater_state_save_atomic(const char *state_path,
    const char *phase,
    const char *install_dir,
    const char *staging,
    const char *backup,
    const char *package)
{
    char tmp[UPDATER_PATH_CAP + 32];
    FILE *fp;
    int n;

    if (state_path == NULL || phase == NULL || install_dir == NULL || staging == NULL || backup == NULL
        || package == NULL) {
        return -1;
    }

    n = snprintf(tmp, sizeof tmp, "%s.part", state_path);
    if (n < 0 || (size_t)n >= sizeof tmp) {
        return -1;
    }

    fp = fopen(tmp, "wb");
    if (fp == NULL) {
        return -1;
    }

    fputs("{\"phase\":", fp);
    json_emit_escaped_string(fp, phase);
    fputs(",\"target\":", fp);
    json_emit_escaped_string(fp, install_dir);
    fputs(",\"staging\":", fp);
    json_emit_escaped_string(fp, staging);
    fputs(",\"backup\":", fp);
    json_emit_escaped_string(fp, backup);
    fputs(",\"package\":", fp);
    json_emit_escaped_string(fp, package);
    fputs("}\n", fp);

    if (fclose(fp) != 0) {
        (void)remove(tmp);
        return -1;
    }

    if (update_move_path(tmp, state_path) != UPDATE_OK) {
        (void)remove(tmp);
        return -1;
    }

    return 0;
}

void updater_state_remove(const char *state_path)
{
    if (state_path == NULL || state_path[0] == '\0') {
        return;
    }

    (void)update_remove_tree(state_path);
}
