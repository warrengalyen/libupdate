#include "platform_fs.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__CYGWIN__)
    #include <windows.h>
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#if defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <stdint.h>
#endif

#ifndef PATH_MAX
    #define PATH_MAX 4096
#endif

#if defined(_WIN32) && !defined(__CYGWIN__)

static int is_path_sep(char c)
{
    return c == '\\' || c == '/';
}

static int map_last_error_to_platform(void)
{
    DWORD e = GetLastError();

    switch (e) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return PLATFORM_ERR_NOT_FOUND;
    case ERROR_ACCESS_DENIED:
        return PLATFORM_ERR_ACCESS;
    case ERROR_ALREADY_EXISTS:
        return PLATFORM_ERR_EXISTS;
    case ERROR_DIRECTORY:
        return PLATFORM_ERR_STATE;
    default:
        return PLATFORM_ERR_IO;
    }
}

static int mkdir_one_win(const char *path)
{
    DWORD err0;

    if (CreateDirectoryA(path, NULL) != 0) {
        return PLATFORM_OK;
    }

    err0 = GetLastError();
    if (err0 == ERROR_ALREADY_EXISTS) {
        DWORD a = GetFileAttributesA(path);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
            return PLATFORM_OK;
        }
    }

    SetLastError(err0);
    return map_last_error_to_platform();
}

int platform_fs_get_executable_path(char *out, size_t out_size)
{
    DWORD n;
    DWORD cap;

    if (out == NULL || out_size == 0U) {
        return PLATFORM_ERR_INVALID_ARG;
    }

    cap = (DWORD)(out_size > (size_t)32767U ? 32767U : out_size);
    n = GetModuleFileNameA(NULL, out, cap);
    if (n == 0U) {
        return PLATFORM_ERR_IO;
    }
    if (n >= cap) {
        if (cap > 0U) {
            out[cap - 1U] = '\0';
        }
        return PLATFORM_ERR_LIMIT;
    }
    out[n] = '\0';
    return PLATFORM_OK;
}

static int strip_filename(char *path)
{
    char *last = NULL;
    size_t i;
    size_t L;

    if (path[0] == '\0') {
        return PLATFORM_ERR_NOT_FOUND;
    }

    for (i = 0; path[i] != '\0'; i++) {
        if (is_path_sep(path[i])) {
            last = path + i;
        }
    }

    if (last == NULL) {
        path[0] = '\0';
        return PLATFORM_OK;
    }

    if (last == path) {
        last[1] = '\0';
        return PLATFORM_OK;
    }

    *last = '\0';

    L = strlen(path);
    if (L == 2U && path[1] == ':') {
        path[2] = '\\';
        path[3] = '\0';
    }

    return PLATFORM_OK;
}

int platform_fs_get_executable_dir(char *out, size_t out_size)
{
    int rc;

    if (out == NULL || out_size == 0U) {
        return PLATFORM_ERR_INVALID_ARG;
    }

    rc = platform_fs_get_executable_path(out, out_size);
    if (rc != PLATFORM_OK) {
        return rc;
    }

    return strip_filename(out);
}

int platform_fs_get_system_temp_dir(char *out, size_t out_size)
{
    DWORD n;
    size_t L;

    if (out == NULL || out_size == 0U) {
        return PLATFORM_ERR_INVALID_ARG;
    }

    n = GetTempPathA((DWORD)out_size, out);
    if (n == 0U || n >= out_size) {
        return PLATFORM_ERR_IO;
    }

    L = (size_t)n;
    while (L > 1U && (out[L - 1U] == '\\' || out[L - 1U] == '/')) {
        L--;
        out[L] = '\0';
    }

    return PLATFORM_OK;
}

int platform_fs_create_directory_recursive(const char *path)
{
    char stack[PATH_MAX];
    size_t len;
    size_t i;
    size_t start;

    if (path == NULL || path[0] == '\0') {
        return PLATFORM_ERR_INVALID_ARG;
    }

    len = strlen(path);
    if (len + 1U > sizeof(stack)) {
        return PLATFORM_ERR_LIMIT;
    }

    memcpy(stack, path, len + 1U);

    for (i = 0; stack[i] != '\0'; i++) {
        if (stack[i] == '/') {
            stack[i] = '\\';
        }
    }

    start = 0U;
    if (len >= 2U && stack[1] == ':') {
        start = 2U;
        if (len > 2U && is_path_sep(stack[2])) {
            start = 3U;
        }
    } else if (is_path_sep(stack[0]) && is_path_sep(stack[1])) {
        /* UNC \\server\share */
        size_t seps = 0U;
        for (i = 0; stack[i] != '\0'; i++) {
            if (is_path_sep(stack[i])) {
                seps++;
                if (seps >= 4U) {
                    start = i + 1U;
                    break;
                }
            }
        }
    }

    for (i = start; stack[i] != '\0'; i++) {
        if (is_path_sep(stack[i])) {
            if (i == start) {
                continue;
            }
            stack[i] = '\0';
            if (stack[start] != '\0') {
                int mk = mkdir_one_win(stack);
                if (mk != PLATFORM_OK) {
                    return mk;
                }
            }
            stack[i] = '\\';
        }
    }

    return mkdir_one_win(stack);
}

int platform_fs_move_path(const char *src, const char *dst)
{
    if (src == NULL || dst == NULL || src[0] == '\0' || dst[0] == '\0') {
        return PLATFORM_ERR_INVALID_ARG;
    }

    if (MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING) == 0) {
        return map_last_error_to_platform();
    }
    return PLATFORM_OK;
}

static int remove_file_win(const char *path)
{
    DWORD a;

    a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) {
        return map_last_error_to_platform();
    }
    if ((a & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
        return PLATFORM_ERR_STATE;
    }
    if (DeleteFileA(path) == 0) {
        return map_last_error_to_platform();
    }
    return PLATFORM_OK;
}

static int remove_dir_recursive_win(const char *dir);

static int remove_dir_contents_win(const char *dir)
{
    char pattern[PATH_MAX];
    size_t dl;
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int rc;

    dl = strlen(dir);
    if (dl + 3U > sizeof(pattern)) {
        return PLATFORM_ERR_LIMIT;
    }

    memcpy(pattern, dir, dl);
    pattern[dl] = '\\';
    pattern[dl + 1U] = '*';
    pattern[dl + 2U] = '\0';

    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return map_last_error_to_platform();
    }

    rc = PLATFORM_OK;
    for (;;) {
        const char *name = fd.cFileName;
        if (name[0] == '.' && name[1] == '\0') {
            /* skip */
        } else if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
            /* skip */
        } else {
            char full[PATH_MAX];
            size_t nl = strlen(name);
            if (dl + 1U + nl + 1U > sizeof(full)) {
                rc = PLATFORM_ERR_LIMIT;
                break;
            }
            memcpy(full, dir, dl);
            full[dl] = '\\';
            memcpy(full + dl + 1U, name, nl + 1U);

            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
                rc = remove_dir_recursive_win(full);
            } else {
                rc = remove_file_win(full);
            }
            if (rc != PLATFORM_OK) {
                break;
            }
        }

        if (FindNextFileA(h, &fd) == 0) {
            DWORD e = GetLastError();
            if (e == ERROR_NO_MORE_FILES) {
                break;
            }
            rc = map_last_error_to_platform();
            break;
        }
    }

    FindClose(h);
    return rc;
}

static int remove_dir_recursive_win(const char *dir)
{
    int rc;

    rc = remove_dir_contents_win(dir);
    if (rc != PLATFORM_OK) {
        return rc;
    }
    if (RemoveDirectoryA(dir) == 0) {
        return map_last_error_to_platform();
    }
    return PLATFORM_OK;
}

int platform_fs_remove_path(const char *path)
{
    DWORD a;

    if (path == NULL || path[0] == '\0') {
        return PLATFORM_ERR_INVALID_ARG;
    }

    a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) {
        return map_last_error_to_platform();
    }
    if ((a & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
        return remove_dir_recursive_win(path);
    }
    return remove_file_win(path);
}

#else /* POSIX */

static int is_path_sep(char c)
{
    return c == '/';
}

static int map_errno_to_platform(void)
{
    switch (errno) {
    case ENOENT:
        return PLATFORM_ERR_NOT_FOUND;
    case EACCES:
    case EPERM:
        return PLATFORM_ERR_ACCESS;
    case EEXIST:
        return PLATFORM_ERR_EXISTS;
    case ENOTEMPTY:
        return PLATFORM_ERR_NOT_EMPTY;
    case ENOMEM:
        return PLATFORM_ERR_NO_MEM;
    case ENAMETOOLONG:
        return PLATFORM_ERR_LIMIT;
    default:
        return PLATFORM_ERR_IO;
    }
}

int platform_fs_get_executable_path(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) {
        return PLATFORM_ERR_INVALID_ARG;
    }

#if defined(__linux__) || defined(__CYGWIN__)
    {
        ssize_t len = readlink("/proc/self/exe", out, out_size - 1U);
        if (len < 0) {
            return map_errno_to_platform();
        }
        out[(size_t)len] = '\0';
    }
    return PLATFORM_OK;
#elif defined(__APPLE__)
    {
        uint32_t sz = (uint32_t)out_size;
        if (_NSGetExecutablePath(out, &sz) != 0) {
            return PLATFORM_ERR_LIMIT;
        }
    }
    return PLATFORM_OK;
#else
    (void)out;
    (void)out_size;
    return PLATFORM_ERR_UNSUPPORTED;
#endif
}

static int strip_filename_posix(char *path)
{
    char *last = NULL;
    size_t i;

    if (path[0] == '\0') {
        return PLATFORM_ERR_NOT_FOUND;
    }

    for (i = 0; path[i] != '\0'; i++) {
        if (is_path_sep(path[i])) {
            last = path + i;
        }
    }

    if (last == NULL) {
        path[0] = '\0';
        return PLATFORM_OK;
    }

    if (last == path) {
        last[1] = '\0';
        return PLATFORM_OK;
    }

    *last = '\0';
    return PLATFORM_OK;
}

int platform_fs_get_executable_dir(char *out, size_t out_size)
{
    int rc;

    if (out == NULL || out_size == 0U) {
        return PLATFORM_ERR_INVALID_ARG;
    }

    rc = platform_fs_get_executable_path(out, out_size);
    if (rc != PLATFORM_OK) {
        return rc;
    }

    return strip_filename_posix(out);
}

int platform_fs_get_system_temp_dir(char *out, size_t out_size)
{
    const char *td;
    size_t L;

    if (out == NULL || out_size == 0U) {
        return PLATFORM_ERR_INVALID_ARG;
    }

    td = getenv("TMPDIR");
    if (td == NULL || td[0] == '\0') {
        td = "/tmp";
    }

    L = strlen(td);
    if (L + 1U > out_size) {
        return PLATFORM_ERR_LIMIT;
    }

    memcpy(out, td, L + 1U);
    while (L > 1U && out[L - 1U] == '/') {
        L--;
        out[L] = '\0';
    }

    return PLATFORM_OK;
}

static int mkdir_one_posix(const char *path)
{
    if (mkdir(path, (mode_t)0755) == 0) {
        return PLATFORM_OK;
    }
    if (errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return PLATFORM_OK;
        }
    }
    return map_errno_to_platform();
}

int platform_fs_create_directory_recursive(const char *path)
{
    char stack[PATH_MAX];
    size_t len;
    size_t i;
    size_t start;

    if (path == NULL || path[0] == '\0') {
        return PLATFORM_ERR_INVALID_ARG;
    }

    len = strlen(path);
    if (len + 1U > sizeof(stack)) {
        return PLATFORM_ERR_LIMIT;
    }

    memcpy(stack, path, len + 1U);

    start = 0U;
    if (stack[0] == '/') {
        start = 1U;
    }

    for (i = start; stack[i] != '\0'; i++) {
        if (is_path_sep(stack[i])) {
            if (i == start) {
                continue;
            }
            stack[i] = '\0';
            if (stack[start] != '\0') {
                int mk = mkdir_one_posix(stack);
                if (mk != PLATFORM_OK) {
                    return mk;
                }
            }
            stack[i] = '/';
        }
    }

    return mkdir_one_posix(stack);
}

int platform_fs_move_path(const char *src, const char *dst)
{
    if (src == NULL || dst == NULL || src[0] == '\0' || dst[0] == '\0') {
        return PLATFORM_ERR_INVALID_ARG;
    }

    if (rename(src, dst) != 0) {
        return map_errno_to_platform();
    }
    return PLATFORM_OK;
}

static int remove_dir_recursive_posix(const char *dir);

static int remove_dir_contents_posix(const char *dir)
{
    DIR *d;
    struct dirent *ent;
    int rc;

    d = opendir(dir);
    if (d == NULL) {
        return map_errno_to_platform();
    }

    rc = PLATFORM_OK;
    for (;;) {
        errno = 0;
        ent = readdir(d);
        if (ent == NULL) {
            if (errno != 0) {
                rc = map_errno_to_platform();
            }
            break;
        }

        if (ent->d_name[0] == '.' && ent->d_name[1] == '\0') {
            continue;
        }
        if (ent->d_name[0] == '.' && ent->d_name[1] == '.' && ent->d_name[2] == '\0') {
            continue;
        }

        {
            char full[PATH_MAX];
            size_t dl = strlen(dir);
            size_t nl = strlen(ent->d_name);
            if (dl + 1U + nl + 1U > sizeof(full)) {
                rc = PLATFORM_ERR_LIMIT;
                break;
            }
            memcpy(full, dir, dl);
            full[dl] = '/';
            memcpy(full + dl + 1U, ent->d_name, nl + 1U);

            {
                struct stat st;
                if (lstat(full, &st) != 0) {
                    rc = map_errno_to_platform();
                    break;
                }
                if (S_ISDIR(st.st_mode)) {
                    rc = remove_dir_recursive_posix(full);
                } else {
                    if (unlink(full) != 0) {
                        rc = map_errno_to_platform();
                    }
                }
            }

            if (rc != PLATFORM_OK) {
                break;
            }
        }
    }

    closedir(d);
    return rc;
}

static int remove_dir_recursive_posix(const char *dir)
{
    int rc;

    rc = remove_dir_contents_posix(dir);
    if (rc != PLATFORM_OK) {
        return rc;
    }
    if (rmdir(dir) != 0) {
        return map_errno_to_platform();
    }
    return PLATFORM_OK;
}

int platform_fs_remove_path(const char *path)
{
    struct stat st;

    if (path == NULL || path[0] == '\0') {
        return PLATFORM_ERR_INVALID_ARG;
    }

    if (lstat(path, &st) != 0) {
        return map_errno_to_platform();
    }

    if (S_ISDIR(st.st_mode)) {
        return remove_dir_recursive_posix(path);
    }

    if (unlink(path) != 0) {
        return map_errno_to_platform();
    }
    return PLATFORM_OK;
}

#endif
