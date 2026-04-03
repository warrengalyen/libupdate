#include "unity.h"
#include "test_helpers.h"
#include "test_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__CYGWIN__)
    #include <windows.h>
#else
    #include <unistd.h>
#endif

#include <update.h>

#include "platform_fs.h"
#include "platform_process.h"

static void sleep_ms(unsigned ms)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
    Sleep((DWORD)ms);
#else
    usleep((unsigned int)ms * 1000U);
#endif
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in;
    FILE *out;
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
    while ((n = fread(buf, 1U, sizeof buf, in)) > 0U) {
        if (fwrite(buf, 1U, n, out) != n) {
            fclose(out);
            fclose(in);
            return -1;
        }
    }
    if (ferror(in) != 0) {
        fclose(out);
        fclose(in);
        return -1;
    }
    fclose(out);
    fclose(in);
    return 0;
}

static void put_env_pair(const char *k, const char *v)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
    TEST_ASSERT_EQUAL_INT(0, _putenv_s(k, v));
#else
    TEST_ASSERT_EQUAL_INT(0, setenv(k, v, 1));
#endif
}

static void clear_env_key(const char *k)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
    TEST_ASSERT_EQUAL_INT(0, _putenv_s(k, ""));
#else
    unsetenv(k);
#endif
}

static int wait_file_exists(const char *path, unsigned attempts, unsigned ms)
{
    unsigned i;
    FILE *fp;

    for (i = 0U; i < attempts; i++) {
        fp = fopen(path, "rb");
        if (fp != NULL) {
            fclose(fp);
            return 0;
        }
        sleep_ms(ms);
    }
    return -1;
}

static void test_perform_child_updates_install_tree(void)
{
    {
        const char *ig = getenv("LIBUPDATE_E2E_PERFORM");
        if (ig == NULL || ig[0] == '\0' || strcmp(ig, "0") == 0) {
            TEST_IGNORE_MESSAGE("Set LIBUPDATE_E2E_PERFORM=1 to run update_perform + updater end-to-end test");
            return;
        }
    }

    char serve[768];
    char pkg[768];
    char inst[768];
    char tmp[768];
    char url[256];
    char child[768];
    char hello[768];
    char marker[768];
    int port = 30000;
    int srv_pid = 0;
    const char *argv_pc[3];
    int pid = 0;
    int exit_code = 1;
    FILE *fp;

    if (LIBUPDATE_PYTHON_EXE[0] == '\0') {
        TEST_IGNORE_MESSAGE("Python not found; perform integration skipped");
        return;
    }

#if defined(_WIN32) && !defined(__CYGWIN__)
    port += (int)(GetCurrentProcessId() % 5000);
#else
    port += (int)(getpid() % 5000);
#endif

    TEST_ASSERT_NOT_NULL(test_env_root());
    TEST_ASSERT_EQUAL_INT(0, test_env_join(serve, sizeof serve, "pf_root"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(pkg, sizeof pkg, "pf_root/pkg.zip"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(inst, sizeof inst, "pf_install"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(tmp, sizeof tmp, "pf_tmp"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(marker, sizeof marker, "pf_install/pre_update_marker.txt"));

    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_create_directory_recursive(serve));
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_create_directory_recursive(inst));
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_create_directory_recursive(tmp));

    fp = fopen(marker, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fputs("keep\n", fp);
    fclose(fp);

    TEST_ASSERT_EQUAL_INT(0, copy_file(LIBUPDATE_TEST_MINIMAL_ZIP, pkg));
    TEST_ASSERT_EQUAL_INT(0, test_write_update_manifest(serve, port, LIBUPDATE_TEST_MINIMAL_SHA256));
    TEST_ASSERT_EQUAL_INT(0, test_http_server_start(serve, port, &srv_pid));
    sleep_ms(250U);

    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/update.json", port);

#if defined(_WIN32) && !defined(__CYGWIN__)
    TEST_ASSERT_EQUAL_INT(0, test_bin_dir_join(child, sizeof child, "libupdate_perform_child.exe"));
#else
    TEST_ASSERT_EQUAL_INT(0, test_bin_dir_join(child, sizeof child, "libupdate_perform_child"));
#endif

    put_env_pair("LIBUPDATE_PERF_URL", url);
    put_env_pair("LIBUPDATE_PERF_INSTALL", inst);
    put_env_pair("LIBUPDATE_PERF_TEMP", tmp);
    put_env_pair("LIBUPDATE_PERF_SHA256", LIBUPDATE_TEST_MINIMAL_SHA256);

    argv_pc[0] = child;
    argv_pc[1] = NULL;
    argv_pc[2] = NULL;
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_process_spawn(child, argv_pc, &pid));
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_process_wait_for_pid_exit(pid, &exit_code));
    TEST_ASSERT_EQUAL_INT(0, exit_code);

    TEST_ASSERT_EQUAL_INT(0, test_env_join(hello, sizeof hello, "pf_install/hello.txt"));
    TEST_ASSERT_EQUAL_INT(0, wait_file_exists(hello, 150U, 100U));

    clear_env_key("LIBUPDATE_PERF_URL");
    clear_env_key("LIBUPDATE_PERF_INSTALL");
    clear_env_key("LIBUPDATE_PERF_TEMP");
    clear_env_key("LIBUPDATE_PERF_SHA256");

    test_http_server_stop(srv_pid);
}

void test_group_apply(void)
{
    RUN_TEST(test_perform_child_updates_install_tree);
}
