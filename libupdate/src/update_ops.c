#include "update.h"

#include "platform_fs.h"
#include "platform_process.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__CYGWIN__)
    #include <windows.h>
#else
    #include <dirent.h>
    #include <signal.h>
    #include <sys/stat.h>
    #include <time.h>
    #include <unistd.h>
#endif

#define UPDATE_OPS_RETRIES 8
#define UPDATE_OPS_RETRY_MS 50U

#ifndef UPDATE_OPS_PATH_MAX
    #define UPDATE_OPS_PATH_MAX 4096
#endif

static void sleep_ms(unsigned ms)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)((ms % 1000U) * 1000000UL);
    (void)nanosleep(&ts, NULL);
#endif
}

UPDATE_API int update_wait_for_parent_exit(int parent_pid)
{
    if (parent_pid <= 0) {
        return UPDATE_ERROR;
    }

#if defined(_WIN32) && !defined(__CYGWIN__)
    {
        DWORD e;

        for (;;) {
            HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)parent_pid);
            if (h != NULL) {
                (void)WaitForSingleObject(h, INFINITE);
                CloseHandle(h);
                return UPDATE_OK;
            }
            e = GetLastError();
            if (e == ERROR_INVALID_PARAMETER) {
                return UPDATE_OK;
            }
            sleep_ms(UPDATE_OPS_RETRY_MS);
        }
    }
#else
    for (;;) {
        if (kill((pid_t)parent_pid, 0) != 0) {
            if (errno == ESRCH) {
                return UPDATE_OK;
            }
            if (errno == EINVAL) {
                return UPDATE_ERROR;
            }
            /* EPERM: process may exist but not signalable; keep polling. */
        }
        sleep_ms(100U);
    }
#endif
}

static int copy_file_once(const char *src, const char *dst)
{
    FILE *in = NULL;
    FILE *out = NULL;
    unsigned char buf[65536];
    size_t n;

    in = fopen(src, "rb");
    if (in == NULL) {
        return -1;
    }

    out = fopen(dst, "wb");
    if (out == NULL) {
        fclose(in);
        return -1;
    }

    while ((n = fread(buf, 1U, sizeof(buf), in)) > 0U) {
        if (fwrite(buf, 1U, n, out) != n) {
            fclose(out);
            fclose(in);
            (void)remove(dst);
            return -1;
        }
    }

    if (ferror(in) != 0) {
        fclose(out);
        fclose(in);
        (void)remove(dst);
        return -1;
    }

    fclose(out);
    fclose(in);
    return 0;
}

static int copy_file_retries(const char *src, const char *dst)
{
    unsigned a;

    for (a = 0U; a < UPDATE_OPS_RETRIES; a++) {
        if (copy_file_once(src, dst) == 0) {
            return 0;
        }
        (void)remove(dst);
        sleep_ms(UPDATE_OPS_RETRY_MS);
    }

    return -1;
}

static int join_path2(const char *a, const char *b, char *out, size_t cap)
{
    int n = snprintf(out, cap, "%s/%s", a, b);
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return 0;
}

#if defined(_WIN32) && !defined(__CYGWIN__)

static int copy_tree_win(const char *src_dir, const char *dst_dir)
{
    char search[UPDATE_OPS_PATH_MAX];
    char src_path[UPDATE_OPS_PATH_MAX];
    char dst_path[UPDATE_OPS_PATH_MAX];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n;

    n = snprintf(search, sizeof(search), "%s\\*", src_dir);
    if (n < 0 || (size_t)n >= sizeof(search)) {
        return -1;
    }

    if (platform_fs_create_directory_recursive(dst_dir) != PLATFORM_OK) {
        return -1;
    }

    h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }

    do {
        if (fd.cFileName[0] == '.' && fd.cFileName[1] == '\0') {
            continue;
        }
        if (fd.cFileName[0] == '.' && fd.cFileName[1] == '.' && fd.cFileName[2] == '\0') {
            continue;
        }

        if (join_path2(src_dir, fd.cFileName, src_path, sizeof(src_path)) != 0) {
            FindClose(h);
            return -1;
        }
        if (join_path2(dst_dir, fd.cFileName, dst_path, sizeof(dst_path)) != 0) {
            FindClose(h);
            return -1;
        }

        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
            if (copy_tree_win(src_path, dst_path) != 0) {
                FindClose(h);
                return -1;
            }
        } else {
            unsigned a;
            for (a = 0U; a < UPDATE_OPS_RETRIES; a++) {
                if (CopyFileA(src_path, dst_path, FALSE) != 0) {
                    break;
                }
                sleep_ms(UPDATE_OPS_RETRY_MS);
            }
            if (a >= UPDATE_OPS_RETRIES) {
                FindClose(h);
                return -1;
            }
        }
    } while (FindNextFileA(h, &fd) != 0);

    FindClose(h);
    return 0;
}

#else /* POSIX */

static int is_dir_posix(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int copy_tree_posix(const char *src_dir, const char *dst_dir)
{
    DIR *d;
    struct dirent *ent;
    int err = 0;

    if (platform_fs_create_directory_recursive(dst_dir) != PLATFORM_OK) {
        return -1;
    }

    d = opendir(src_dir);
    if (d == NULL) {
        return -1;
    }

    for (;;) {
        errno = 0;
        ent = readdir(d);
        if (ent == NULL) {
            if (errno != 0) {
                err = -1;
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
            char src_path[UPDATE_OPS_PATH_MAX];
            char dst_path[UPDATE_OPS_PATH_MAX];

            if (join_path2(src_dir, ent->d_name, src_path, sizeof(src_path)) != 0) {
                err = -1;
                break;
            }
            if (join_path2(dst_dir, ent->d_name, dst_path, sizeof(dst_path)) != 0) {
                err = -1;
                break;
            }

            if (is_dir_posix(src_path) != 0) {
                if (copy_tree_posix(src_path, dst_path) != 0) {
                    err = -1;
                    break;
                }
            } else {
                if (copy_file_retries(src_path, dst_path) != 0) {
                    err = -1;
                    break;
                }
            }
        }
    }

    closedir(d);
    return err;
}

#endif

UPDATE_API int update_copy_tree(const char *src_dir, const char *dst_dir)
{
    unsigned a;

    if (src_dir == NULL || src_dir[0] == '\0' || dst_dir == NULL || dst_dir[0] == '\0') {
        return UPDATE_ERROR;
    }

    for (a = 0U; a < UPDATE_OPS_RETRIES; a++) {
#if defined(_WIN32) && !defined(__CYGWIN__)
        if (copy_tree_win(src_dir, dst_dir) == 0) {
            return UPDATE_OK;
        }
#else
        if (copy_tree_posix(src_dir, dst_dir) == 0) {
            return UPDATE_OK;
        }
#endif
        sleep_ms(UPDATE_OPS_RETRY_MS);
    }

    return UPDATE_ERROR;
}

UPDATE_API int update_remove_tree(const char *path)
{
    unsigned a;
    int pr;

    if (path == NULL || path[0] == '\0') {
        return UPDATE_ERROR;
    }

    for (a = 0U; a < UPDATE_OPS_RETRIES; a++) {
        pr = platform_fs_remove_path(path);
        if (pr == PLATFORM_OK) {
            return UPDATE_OK;
        }
        sleep_ms(UPDATE_OPS_RETRY_MS);
    }

    return UPDATE_ERROR;
}

UPDATE_API int update_move_path(const char *src, const char *dst)
{
    unsigned a;
    int pr;

    if (src == NULL || src[0] == '\0' || dst == NULL || dst[0] == '\0') {
        return UPDATE_ERROR;
    }

    for (a = 0U; a < UPDATE_OPS_RETRIES; a++) {
        pr = platform_fs_move_path(src, dst);
        if (pr == PLATFORM_OK) {
            return UPDATE_OK;
        }
        sleep_ms(UPDATE_OPS_RETRY_MS);
    }

    return UPDATE_ERROR;
}

UPDATE_API int update_relaunch_app(const char *executable_path)
{
    const char *const argv[2] = { executable_path, NULL };
    int pid = 0;

    if (executable_path == NULL || executable_path[0] == '\0') {
        return UPDATE_ERROR;
    }

    if (platform_process_spawn(executable_path, argv, &pid) != PLATFORM_OK) {
        return UPDATE_ERROR;
    }

    return UPDATE_OK;
}
