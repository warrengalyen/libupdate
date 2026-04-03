#include "test_helpers.h"
#include "test_config.h"
#include "test_log.h"

#include "platform_fs.h"
#include "platform_process.h"

#include <update.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32) && !defined(__CYGWIN__)
    #include <windows.h>
#else
    #include <signal.h>
    #include <unistd.h>
#endif

static char s_root[1024];

static int random_tag(void)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
    return (int)(GetTickCount() ^ (GetCurrentProcessId() << 16));
#else
    return (int)(getpid() ^ (int)time(NULL));
#endif
}

void test_env_setup(void)
{
    char base[768];
    char sub[320];
    int tag = random_tag();

    s_root[0] = '\0';
    if (platform_fs_get_system_temp_dir(base, sizeof base) != PLATFORM_OK) {
        return;
    }

#if defined(_WIN32) && !defined(__CYGWIN__)
    if (snprintf(sub, sizeof sub, "libupdate_ut_%d_%d", tag, (int)GetCurrentProcessId()) >= (int)sizeof sub) {
        return;
    }
    if (snprintf(s_root, sizeof s_root, "%s\\%s", base, sub) >= (int)sizeof s_root) {
        return;
    }
    if (platform_fs_create_directory_recursive(s_root) != PLATFORM_OK) {
        s_root[0] = '\0';
        return;
    }
#else
    if (snprintf(sub, sizeof sub, "libupdate_ut_%d_%d", tag, (int)getpid()) >= (int)sizeof sub) {
        return;
    }
    if (snprintf(s_root, sizeof s_root, "%s/%s", base, sub) >= (int)sizeof s_root) {
        return;
    }
    if (platform_fs_create_directory_recursive(s_root) != PLATFORM_OK) {
        s_root[0] = '\0';
        return;
    }
#endif
    LIBUPDATE_TEST_LOG("test_env_setup: %s\n", s_root);
}

void test_env_cleanup(void)
{
    if (s_root[0] != '\0') {
        (void)update_remove_tree(s_root);
        LIBUPDATE_TEST_LOG("test_env_cleanup: %s\n", s_root);
    }
    s_root[0] = '\0';
}

const char *test_env_root(void)
{
    return s_root[0] != '\0' ? s_root : NULL;
}

int test_env_join(char *out, size_t cap, const char *rel)
{
    const char *r = test_env_root();

    if (r == NULL || rel == NULL || out == NULL || cap == 0U) {
        return -1;
    }

#if defined(_WIN32) && !defined(__CYGWIN__)
    if (snprintf(out, cap, "%s\\%s", r, rel) >= (int)cap) {
        return -1;
    }
#else
    if (snprintf(out, cap, "%s/%s", r, rel) >= (int)cap) {
        return -1;
    }
#endif
    return 0;
}

int test_bin_dir_join(char *out, size_t cap, const char *name)
{
    char exe[1024];

    if (out == NULL || cap == 0U || name == NULL) {
        return -1;
    }

    if (platform_fs_get_executable_path(exe, sizeof exe) != PLATFORM_OK) {
        return -1;
    }

#if defined(_WIN32) && !defined(__CYGWIN__)
    {
        char *slash = strrchr(exe, '\\');
        if (slash == NULL) {
            slash = strrchr(exe, '/');
        }
        if (slash == NULL) {
            return -1;
        }
        *slash = '\0';
        if (snprintf(out, cap, "%s\\%s", exe, name) >= (int)cap) {
            return -1;
        }
    }
#else
    {
        char *slash = strrchr(exe, '/');
        if (slash == NULL) {
            return -1;
        }
        *slash = '\0';
        if (snprintf(out, cap, "%s/%s", exe, name) >= (int)cap) {
            return -1;
        }
    }
#endif
    return 0;
}

int test_http_server_start(const char *serve_root, int port, int *out_pid)
{
    char portbuf[32];
    int pid = 0;
    const char *py;
    const char *argv[6];

    if (serve_root == NULL || out_pid == NULL) {
        return -1;
    }

    py = LIBUPDATE_PYTHON_EXE;
    if (py == NULL || py[0] == '\0') {
        return -1;
    }

    (void)snprintf(portbuf, sizeof portbuf, "%d", port);

    argv[0] = py;
    argv[1] = LIBUPDATE_HTTP_SERVE_PY;
    argv[2] = serve_root;
    argv[3] = portbuf;
    argv[4] = NULL;

    if (platform_process_spawn(py, (const char *const *)argv, &pid) != PLATFORM_OK) {
        return -1;
    }

    *out_pid = pid;
    return 0;
}

void test_http_server_stop(int pid)
{
    if (pid <= 0) {
        return;
    }
#if defined(_WIN32) && !defined(__CYGWIN__)
    {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
        if (h != NULL) {
            (void)TerminateProcess(h, 1);
            CloseHandle(h);
        }
    }
#else
    (void)kill((pid_t)pid, SIGTERM);
#endif
}

int test_write_update_manifest(const char *serve_root, int port, const char *sha256_hex)
{
    FILE *in;
    FILE *out;
    char outpath[1024];
    char buf[4096];
    size_t n;
    char portbuf[32];

    if (serve_root == NULL || sha256_hex == NULL) {
        return -1;
    }

    (void)snprintf(portbuf, sizeof portbuf, "%d", port);

#if defined(_WIN32) && !defined(__CYGWIN__)
    if (snprintf(outpath, sizeof outpath, "%s\\update.json", serve_root) >= (int)sizeof outpath) {
        return -1;
    }
#else
    if (snprintf(outpath, sizeof outpath, "%s/update.json", serve_root) >= (int)sizeof outpath) {
        return -1;
    }
#endif

    in = fopen(LIBUPDATE_FIXTURE_UPDATE_JSON, "rb");
    if (in == NULL) {
        return -1;
    }

    n = fread(buf, 1U, sizeof(buf) - 1U, in);
    (void)fclose(in);
    buf[n] = '\0';

    out = fopen(outpath, "wb");
    if (out == NULL) {
        return -1;
    }

    /* Simple in-buffer replace (template is small). */
    {
        char tmp[4096];
        char *d = tmp;
        const char *s = buf;
        const char *end = buf + strlen(buf);
        size_t rem = sizeof tmp;

        while (s < end && rem > 1U) {
            if (strncmp(s, "PORT_PLACEHOLDER", 16) == 0) {
                int w = snprintf(d, rem, "%s", portbuf);
                if (w < 0 || (size_t)w >= rem) {
                    fclose(out);
                    return -1;
                }
                d += (size_t)w;
                rem -= (size_t)w;
                s += 16;
                continue;
            }
            if (strncmp(s, "CHECKSUM_PLACEHOLDER", 20) == 0) {
                int w = snprintf(d, rem, "%s", sha256_hex);
                if (w < 0 || (size_t)w >= rem) {
                    fclose(out);
                    return -1;
                }
                d += (size_t)w;
                rem -= (size_t)w;
                s += 20;
                continue;
            }
            *d++ = *s++;
            rem--;
        }
        *d = '\0';
        if (fputs(tmp, out) < 0) {
            fclose(out);
            return -1;
        }
    }

    if (fclose(out) != 0) {
        return -1;
    }

    return 0;
}
