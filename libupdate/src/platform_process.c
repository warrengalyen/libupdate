#include "platform_process.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__CYGWIN__)
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <signal.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

#if defined(_WIN32) && !defined(__CYGWIN__)

static int win_build_cmdline(const char *const *argv, char **out_cmdline)
{
    size_t total;
    size_t pos;
    size_t j;
    char *buf;

    if (argv == NULL || argv[0] == NULL) {
        return PLATFORM_ERR_INVALID_ARG;
    }

    total = 1U;
    for (j = 0; argv[j] != NULL; j++) {
        total += strlen(argv[j]) * 2U + 3U;
    }

    buf = (char *)malloc(total);
    if (buf == NULL) {
        return PLATFORM_ERR_NO_MEM;
    }

    pos = 0U;
    for (j = 0; argv[j] != NULL; j++) {
        const char *a = argv[j];
        int need_quote = (strchr(a, ' ') != NULL) || (strchr(a, '\t') != NULL) || (a[0] == '\0');

        if (pos > 0U) {
            buf[pos++] = ' ';
        }

        if (need_quote) {
            buf[pos++] = '"';
            for (; *a != '\0'; a++) {
                if (*a == '"') {
                    if (pos + 2U >= total) {
                        free(buf);
                        return PLATFORM_ERR_LIMIT;
                    }
                    buf[pos++] = '\\';
                }
                buf[pos++] = *a;
            }
            buf[pos++] = '"';
        } else {
            for (; *a != '\0'; a++) {
                if (pos + 1U >= total) {
                    free(buf);
                    return PLATFORM_ERR_LIMIT;
                }
                buf[pos++] = *a;
            }
        }
    }

    if (pos >= total) {
        free(buf);
        return PLATFORM_ERR_LIMIT;
    }

    buf[pos] = '\0';
    *out_cmdline = buf;
    return PLATFORM_OK;
}

int platform_process_spawn(const char *path, const char *const *argv, int *out_pid)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char *cmdline;
    int build_rc;
    DWORD flags;

    if (path == NULL || path[0] == '\0' || argv == NULL || out_pid == NULL) {
        return PLATFORM_ERR_INVALID_ARG;
    }

    build_rc = win_build_cmdline(argv, &cmdline);
    if (build_rc != PLATFORM_OK) {
        return build_rc;
    }

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    flags = 0;
    if (CreateProcessA(path, cmdline, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi) == 0) {
        free(cmdline);
        return PLATFORM_ERR_IO;
    }

    free(cmdline);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    *out_pid = (int)pi.dwProcessId;
    return PLATFORM_OK;
}

int platform_process_get_current_pid(void)
{
    return (int)GetCurrentProcessId();
}

int platform_process_wait_for_pid_exit(int pid, int *out_exit_code)
{
    HANDLE h;
    DWORD wait_rc;
    DWORD exit_code;

    if (pid <= 0) {
        return PLATFORM_ERR_INVALID_ARG;
    }

    h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
    if (h == NULL) {
        return PLATFORM_ERR_NOT_FOUND;
    }

    wait_rc = WaitForSingleObject(h, INFINITE);
    if (wait_rc != WAIT_OBJECT_0) {
        CloseHandle(h);
        return PLATFORM_ERR_IO;
    }

    if (GetExitCodeProcess(h, &exit_code) == 0) {
        CloseHandle(h);
        return PLATFORM_ERR_IO;
    }

    CloseHandle(h);

    if (out_exit_code != NULL) {
        if (exit_code > 255U) {
            *out_exit_code = (int)255;
        } else {
            *out_exit_code = (int)exit_code;
        }
    }

    return PLATFORM_OK;
}

#else /* POSIX */

int platform_process_spawn(const char *path, const char *const *argv, int *out_pid)
{
    pid_t pid;
    size_t n;
    size_t i;
    char **av;
    int pfd[2];
    int pipe_rc;
    ssize_t rr;
    char probe;

    if (path == NULL || path[0] == '\0' || argv == NULL || out_pid == NULL) {
        return PLATFORM_ERR_INVALID_ARG;
    }

    n = 0U;
    while (argv[n] != NULL) {
        n++;
    }
    if (n == 0U) {
        return PLATFORM_ERR_INVALID_ARG;
    }

    av = (char **)malloc((n + 1U) * sizeof(char *));
    if (av == NULL) {
        return PLATFORM_ERR_NO_MEM;
    }

    for (i = 0U; i < n; i++) {
        av[i] = strdup(argv[i]);
        if (av[i] == NULL) {
            while (i > 0U) {
                i--;
                free(av[i]);
            }
            free(av);
            return PLATFORM_ERR_NO_MEM;
        }
    }
    av[n] = NULL;

    pipe_rc = pipe(pfd);
    if (pipe_rc != 0) {
        for (i = 0U; i < n; i++) {
            free(av[i]);
        }
        free(av);
        return PLATFORM_ERR_IO;
    }

    pid = fork();
    if (pid < 0) {
        (void)close(pfd[0]);
        (void)close(pfd[1]);
        for (i = 0U; i < n; i++) {
            free(av[i]);
        }
        free(av);
        return PLATFORM_ERR_IO;
    }

    if (pid == 0) {
        (void)signal(SIGPIPE, SIG_DFL);
        (void)close(pfd[0]);
        (void)fcntl(pfd[1], F_SETFD, FD_CLOEXEC);
        (void)execv(path, av);
        probe = 1;
        (void)write(pfd[1], &probe, 1);
        (void)close(pfd[1]);
        _exit(127);
    }

    (void)close(pfd[1]);
    for (;;) {
        rr = read(pfd[0], &probe, 1);
        if (rr >= 0 || errno != EINTR) {
            break;
        }
    }
    (void)close(pfd[0]);

    if (rr != 0) {
        (void)waitpid(pid, NULL, 0);
        for (i = 0U; i < n; i++) {
            free(av[i]);
        }
        free(av);
        return PLATFORM_ERR_IO;
    }

    for (i = 0U; i < n; i++) {
        free(av[i]);
    }
    free(av);

    *out_pid = (int)pid;
    return PLATFORM_OK;
}

int platform_process_get_current_pid(void)
{
    return (int)getpid();
}

int platform_process_wait_for_pid_exit(int pid, int *out_exit_code)
{
    int status;
    pid_t r;

    if (pid <= 0) {
        return PLATFORM_ERR_INVALID_ARG;
    }

    for (;;) {
        r = waitpid((pid_t)pid, &status, 0);
        if (r == (pid_t)pid) {
            break;
        }
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ECHILD) {
                return PLATFORM_ERR_NOT_FOUND;
            }
            return PLATFORM_ERR_IO;
        }
    }

    if (out_exit_code != NULL) {
        if (WIFEXITED(status)) {
            *out_exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            *out_exit_code = 128 + WTERMSIG(status);
        } else {
            *out_exit_code = 0;
        }
    }

    return PLATFORM_OK;
}

#endif
