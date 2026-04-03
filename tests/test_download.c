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

static void test_check_and_download_from_mock_server(void)
{
    const char *ig = getenv("LIBUPDATE_INTEGRATION_TESTS");

    if (ig == NULL || ig[0] == '\0' || strcmp(ig, "0") == 0) {
        TEST_IGNORE_MESSAGE("Set LIBUPDATE_INTEGRATION_TESTS=1 to run mock HTTP server test");
        return;
    }

    char serve[768];
    char pkg[768];
    char inst[768];
    char tmp[768];
    char dl[768];
    char url[256];
    int port = 22000;
    int srv_pid = 0;
    update_options_t o;
    update_info_t info;
    int st;
    FILE *fp;
    char sig[8];

    if (LIBUPDATE_PYTHON_EXE[0] == '\0') {
        TEST_IGNORE_MESSAGE("Python not found; HTTP integration test skipped");
        return;
    }

#if defined(_WIN32) && !defined(__CYGWIN__)
    {
        const char *wh = getenv("LIBUPDATE_INTEGRATION_WINHTTP");
        if (wh == NULL || wh[0] == '\0' || strcmp(wh, "0") == 0) {
            TEST_IGNORE_MESSAGE(
                "Native Windows: also set LIBUPDATE_INTEGRATION_WINHTTP=1 to run WinHTTP mock test (otherwise skipped)");
            return;
        }
    }
#endif

#if defined(_WIN32) && !defined(__CYGWIN__)
    port += (int)(GetCurrentProcessId() % 8000);
#else
    port += (int)(getpid() % 8000);
#endif

    TEST_ASSERT_NOT_NULL(test_env_root());
    TEST_ASSERT_EQUAL_INT(0, test_env_join(serve, sizeof serve, "http_root"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(pkg, sizeof pkg, "http_root/pkg.zip"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(inst, sizeof inst, "install"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(tmp, sizeof tmp, "tmp"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(dl, sizeof dl, "tmp/downloaded.json"));

    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_create_directory_recursive(serve));
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_create_directory_recursive(inst));
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_create_directory_recursive(tmp));

    TEST_ASSERT_EQUAL_INT(0, copy_file(LIBUPDATE_TEST_MINIMAL_ZIP, pkg));
    TEST_ASSERT_EQUAL_INT(0, test_write_update_manifest(serve, port, LIBUPDATE_TEST_MINIMAL_SHA256));

    TEST_ASSERT_EQUAL_INT(0, test_http_server_start(serve, port, &srv_pid));
    sleep_ms(250U);

    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/update.json", port);

    memset(&o, 0, sizeof(o));
    o.update_url = url;
    o.app_name = "http_test";
    o.install_dir = inst;
    o.temp_dir = tmp;

    TEST_ASSERT_EQUAL_INT(UPDATE_OK, update_init(&o));
    st = update_check(&info);
    TEST_ASSERT_EQUAL_INT(UPDATE_AVAILABLE, st);
    TEST_ASSERT_TRUE(strlen(info.download_url) > 0U);
    TEST_ASSERT_TRUE(strlen(info.checksum) > 0U);

    TEST_ASSERT_EQUAL_INT(UPDATE_OK, update_download(info.download_url, dl));
    fp = fopen(dl, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_TRUE(fread(sig, 1U, sizeof sig, fp) > 0U);
    fclose(fp);

    test_http_server_stop(srv_pid);
}

void test_group_download(void)
{
    RUN_TEST(test_check_and_download_from_mock_server);
}
