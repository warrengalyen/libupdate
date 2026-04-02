#include "update.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UPDATE_PATH_MAX
    #if defined(PATH_MAX) && PATH_MAX > 4096
        #define UPDATE_PATH_MAX PATH_MAX
    #else
        #define UPDATE_PATH_MAX 4096
    #endif
#endif

static int path_has_bad_chars(const char *path)
{
    const unsigned char *p;

    if (path == NULL) {
        return 1;
    }

    for (p = (const unsigned char *)path; *p != '\0'; p++) {
        if (*p < 32U) {
            return 1;
        }
#if defined(_WIN32) && !defined(__CYGWIN__)
        if (*p == '<' || *p == '>' || *p == '|' || *p == '*' || *p == '?' || *p == '"') {
            return 1;
        }
#endif
    }

    return 0;
}

static int path_segment_invalid(const char *start, size_t len)
{
    if (len == 0U) {
        return 1;
    }
    if (len == 1U && start[0] == '.') {
        return 1;
    }
    if (len == 2U && start[0] == '.' && start[1] == '.') {
        return 1;
    }
    return 0;
}

static int path_rejects_dot_segments(const char *path)
{
    const char *p = path;

    if (path == NULL) {
        return 1;
    }

    while (*p == '/' || *p == '\\') {
        p++;
    }

    while (*p != '\0') {
        const char *start = p;
        while (*p != '\0' && *p != '/' && *p != '\\') {
            p++;
        }
        {
            size_t seglen = (size_t)(p - start);
            if (seglen == 0U) {
                /* repeated or leading separators */
            } else if (path_segment_invalid(start, seglen) != 0) {
                return 1;
            }
        }
        while (*p == '/' || *p == '\\') {
            p++;
        }
    }

    return 0;
}

static int path_is_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return 0;
    }

#if defined(_WIN32) && !defined(__CYGWIN__)
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':'
        && (path[2] == '\\' || path[2] == '/')) {
        return 1;
    }
    if (path[0] == '\\' && path[1] == '\\') {
        return 1;
    }
    return 0;
#else
    return path[0] == '/';
#endif
}

#if defined(_WIN32) && !defined(__CYGWIN__)

static int win_reserved_name(const char *name, size_t len)
{
    char buf[16];
    size_t i;
    size_t base_len;

    if (len == 0U || len + 1U > sizeof(buf)) {
        return 0;
    }

    memcpy(buf, name, len);
    buf[len] = '\0';
    for (i = 0U; buf[i] != '\0'; i++) {
        if (buf[i] >= 'a' && buf[i] <= 'z') {
            buf[i] = (char)(buf[i] - 'a' + 'A');
        }
    }

    base_len = strcspn(buf, ".");
    if (base_len == 0U) {
        return 0;
    }

    if (base_len == 3U && (strncmp(buf, "CON", 3) == 0 || strncmp(buf, "PRN", 3) == 0 || strncmp(buf, "AUX", 3) == 0
            || strncmp(buf, "NUL", 3) == 0)) {
        return 1;
    }

    if (base_len == 4U && strncmp(buf, "COM", 3) == 0 && buf[3] >= '1' && buf[3] <= '9') {
        return 1;
    }

    if (base_len == 4U && strncmp(buf, "LPT", 3) == 0 && buf[3] >= '1' && buf[3] <= '9') {
        return 1;
    }

    return 0;
}

static int path_rejects_win_reserved(const char *path)
{
    const char *p = path;
    const char *base = path;

    if (path == NULL) {
        return 1;
    }

    for (; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }

    {
        size_t L = strlen(base);
        size_t dot = strcspn(base, ".");
        size_t cmp_len = dot < L ? dot : L;
        if (win_reserved_name(base, cmp_len) != 0) {
            return 1;
        }
    }

    return 0;
}

#endif

UPDATE_API int update_validate_path(const char *path, unsigned flags)
{
    size_t L;

    if (path == NULL || path[0] == '\0') {
        return UPDATE_ERROR;
    }

    L = strlen(path);
    if (L == 0U || L >= UPDATE_PATH_MAX) {
        return UPDATE_ERROR;
    }

    if (path_has_bad_chars(path) != 0) {
        return UPDATE_ERROR;
    }

    if (path_rejects_dot_segments(path) != 0) {
        return UPDATE_ERROR;
    }

    if ((flags & 1U) != 0U && path_is_absolute(path) == 0) {
        return UPDATE_ERROR;
    }

#if defined(_WIN32) && !defined(__CYGWIN__)
    if (path_rejects_win_reserved(path) != 0) {
        return UPDATE_ERROR;
    }
#endif

    return UPDATE_OK;
}

UPDATE_API int update_validate_install_paths(const char *zip_path,
    const char *install_dir,
    const char *staging_dir,
    const char *backup_dir,
    const char *state_path)
{
    char expect_staging[UPDATE_PATH_MAX];
    char expect_backup[UPDATE_PATH_MAX];
    char expect_state[UPDATE_PATH_MAX];
    int n;

    if (zip_path == NULL || install_dir == NULL || staging_dir == NULL || backup_dir == NULL
        || state_path == NULL) {
        return UPDATE_ERROR;
    }

    if (update_validate_path(zip_path, 0U) != UPDATE_OK) {
        return UPDATE_ERROR;
    }

    if (update_validate_path(install_dir, 1U) != UPDATE_OK) {
        return UPDATE_ERROR;
    }

    n = snprintf(expect_staging, sizeof expect_staging, "%s.update_staging", install_dir);
    if (n < 0 || (size_t)n >= sizeof expect_staging) {
        return UPDATE_ERROR;
    }

    n = snprintf(expect_backup, sizeof expect_backup, "%s.update_backup", install_dir);
    if (n < 0 || (size_t)n >= sizeof expect_backup) {
        return UPDATE_ERROR;
    }

    n = snprintf(expect_state, sizeof expect_state, "%s.update_state.json", install_dir);
    if (n < 0 || (size_t)n >= sizeof expect_state) {
        return UPDATE_ERROR;
    }

    if (strcmp(staging_dir, expect_staging) != 0 || strcmp(backup_dir, expect_backup) != 0
        || strcmp(state_path, expect_state) != 0) {
        return UPDATE_ERROR;
    }

    if (update_validate_path(staging_dir, 1U) != UPDATE_OK || update_validate_path(backup_dir, 1U) != UPDATE_OK
        || update_validate_path(state_path, 1U) != UPDATE_OK) {
        return UPDATE_ERROR;
    }

    return UPDATE_OK;
}
