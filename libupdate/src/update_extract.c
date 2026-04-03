#include "update.h"

#include "platform_fs.h"

#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__CYGWIN__)
    #include <windows.h>
#else
    #include <sys/stat.h>
#endif

#ifndef UPDATE_EXTRACT_PATH_MAX
    #if defined(PATH_MAX) && PATH_MAX > 4096
        #define UPDATE_EXTRACT_PATH_MAX PATH_MAX
    #else
        #define UPDATE_EXTRACT_PATH_MAX 4096
    #endif
#endif

static int path_canonicalize_dest(const char *path, char *out, size_t cap)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
    DWORD n;
    if (path == NULL || out == NULL || cap == 0U) {
        return -1;
    }
    n = GetFullPathNameA(path, (DWORD)cap, out, NULL);
    if (n == 0U || n >= (DWORD)cap) {
        return -1;
    }
    return 0;
#else
    if (path == NULL || out == NULL) {
        return -1;
    }
    if (realpath(path, out) == NULL) {
        return -1;
    }
    return 0;
#endif
}

static size_t strip_trailing_seps_len(const char *s, size_t len)
{
    while (len > 0U && (s[len - 1U] == '/' || s[len - 1U] == '\\')) {
        len--;
    }
    return len;
}

static int path_under_dest_prefix(const char *dest_canon, const char *joined_path)
{
    size_t ld;
    size_t lj;

    if (dest_canon == NULL || joined_path == NULL) {
        return 0;
    }

    ld = strlen(dest_canon);
    lj = strlen(joined_path);
    ld = strip_trailing_seps_len(dest_canon, ld);

    if (lj < ld + 1U) {
        return 0;
    }

#if defined(_WIN32) && !defined(__CYGWIN__)
    if (_strnicmp(dest_canon, joined_path, ld) != 0) {
        return 0;
    }
#else
    if (strncmp(dest_canon, joined_path, ld) != 0) {
        return 0;
    }
#endif

    if (joined_path[ld] != '/' && joined_path[ld] != '\\') {
        return 0;
    }

    return 1;
}

static int zip_entry_name_safe(const char *name)
{
    const char *p;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }

    if (name[0] == '/' || name[0] == '\\') {
        return 0;
    }

#if defined(_WIN32) && !defined(__CYGWIN__)
    if (((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')) && name[1] == ':'
        && (name[2] == '\\' || name[2] == '/')) {
        return 0;
    }
#endif

    if (name[0] == '\\' && name[1] == '\\') {
        return 0;
    }

    if (strstr(name, "../") != NULL || strstr(name, "..\\") != NULL) {
        return 0;
    }

    p = name;
    while (*p != '\0') {
        const char *start = p;
        while (*p != '\0' && *p != '/' && *p != '\\') {
            p++;
        }
        {
            size_t seglen = (size_t)(p - start);
            if (seglen == 0U) {
                return 0;
            }
            if (seglen == 2U && start[0] == '.' && start[1] == '.') {
                return 0;
            }
            if (seglen == 1U && start[0] == '.') {
                return 0;
            }
        }
        while (*p == '/' || *p == '\\') {
            p++;
        }
    }

    return 1;
}

static int join_dest_entry(const char *dest_canon, const char *entry, char *out, size_t cap)
{
    int n;

    if (dest_canon == NULL || entry == NULL || out == NULL || cap == 0U) {
        return -1;
    }

    n = snprintf(out, cap, "%s/%s", dest_canon, entry);
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }

    return 0;
}

static void trim_trailing_slash_inplace(char *s)
{
    size_t L = strlen(s);
    while (L > 0U && (s[L - 1U] == '/' || s[L - 1U] == '\\')) {
        s[L - 1U] = '\0';
        L--;
    }
}

static int ensure_parent_dirs(const char *file_path)
{
    char parent[UPDATE_EXTRACT_PATH_MAX];
    char *slash;

    if (strlen(file_path) + 1U > sizeof(parent)) {
        return -1;
    }

    memcpy(parent, file_path, strlen(file_path) + 1U);
    slash = strrchr(parent, '/');
#if defined(_WIN32) && !defined(__CYGWIN__)
    {
        char *bs = strrchr(parent, '\\');
        if (slash == NULL || (bs != NULL && bs > slash)) {
            slash = bs;
        }
    }
#endif
    if (slash == NULL || slash == parent) {
        return 0;
    }

    *slash = '\0';
    if (parent[0] == '\0') {
        return 0;
    }

    return platform_fs_create_directory_recursive(parent) == PLATFORM_OK ? 0 : -1;
}

UPDATE_API int update_extract(const char *zip_path, const char *dest_dir)
{
    mz_zip_archive zip;
    char dest_canon[UPDATE_EXTRACT_PATH_MAX];
    mz_uint i;
    mz_uint n;
    int rc = UPDATE_ERROR;

    if (zip_path == NULL || zip_path[0] == '\0' || dest_dir == NULL || dest_dir[0] == '\0') {
        return UPDATE_ERROR;
    }

    if (update_validate_path(zip_path, 0U) != UPDATE_OK || update_validate_path(dest_dir, 0U) != UPDATE_OK) {
        return UPDATE_ERROR;
    }

    if (platform_fs_create_directory_recursive(dest_dir) != PLATFORM_OK) {
        return UPDATE_ERROR;
    }

    if (path_canonicalize_dest(dest_dir, dest_canon, sizeof(dest_canon)) != 0) {
        return UPDATE_ERROR;
    }

    memset(&zip, 0, sizeof(zip));
    if (mz_zip_reader_init_file(&zip, zip_path, 0) == MZ_FALSE) {
        return UPDATE_ERROR;
    }

    n = mz_zip_reader_get_num_files(&zip);
    for (i = 0U; i < n; i++) {
        mz_zip_archive_file_stat st;
        char full_path[UPDATE_EXTRACT_PATH_MAX];
        char tmp_path[UPDATE_EXTRACT_PATH_MAX + 32];
        mz_bool is_dir;

        if (mz_zip_reader_file_stat(&zip, i, &st) == MZ_FALSE) {
            goto cleanup_zip;
        }

        if ((st.m_bit_flag & 1U) != 0U) {
            goto cleanup_zip;
        }

        if (zip_entry_name_safe(st.m_filename) == 0) {
            goto cleanup_zip;
        }

        if (join_dest_entry(dest_canon, st.m_filename, full_path, sizeof(full_path)) != 0) {
            goto cleanup_zip;
        }

        if (path_under_dest_prefix(dest_canon, full_path) == 0) {
            goto cleanup_zip;
        }

        is_dir = mz_zip_reader_is_file_a_directory(&zip, i);
        if (is_dir == MZ_TRUE) {
            trim_trailing_slash_inplace(full_path);
            if (platform_fs_create_directory_recursive(full_path) != PLATFORM_OK) {
                goto cleanup_zip;
            }
#if !defined(_WIN32) || defined(__CYGWIN__)
            {
                mz_uint32 ext = st.m_external_attr;
                unsigned mode = (unsigned)((ext >> 16) & 07777U);
                if (mode != 0U) {
                    (void)platform_fs_chmod(full_path, mode);
                }
            }
#endif
            continue;
        }

        if (snprintf(tmp_path, sizeof(tmp_path), "%s.extracting", full_path) >= (int)sizeof(tmp_path)) {
            goto cleanup_zip;
        }

        (void)platform_fs_remove_path(tmp_path);

        if (ensure_parent_dirs(full_path) != 0) {
            goto cleanup_zip;
        }

        if (mz_zip_reader_extract_to_file(&zip, i, tmp_path, 0) == MZ_FALSE) {
            (void)platform_fs_remove_path(tmp_path);
            goto cleanup_zip;
        }

#if !defined(_WIN32) || defined(__CYGWIN__)
        {
            mz_uint32 ext = st.m_external_attr;
            unsigned mode = (unsigned)((ext >> 16) & 07777U);
            if (mode != 0U) {
                (void)platform_fs_chmod(tmp_path, mode);
            }
        }
#endif

        if (platform_fs_move_path(tmp_path, full_path) != PLATFORM_OK) {
            (void)platform_fs_remove_path(tmp_path);
            goto cleanup_zip;
        }
    }

    rc = UPDATE_OK;

cleanup_zip:
    (void)mz_zip_reader_end(&zip);
    return rc;
}
