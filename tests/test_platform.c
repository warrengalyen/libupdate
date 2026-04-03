#include "unity.h"
#include "test_helpers.h"

#include <stdlib.h>
#include <string.h>

#include "platform_fs.h"
#include "platform_process.h"

static void test_get_executable_path_non_empty(void)
{
    char buf[1024];

    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_get_executable_path(buf, sizeof buf));
    TEST_ASSERT_TRUE(strlen(buf) > 10U);
}

static void test_system_temp_dir(void)
{
    char buf[512];

    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_get_system_temp_dir(buf, sizeof buf));
    TEST_ASSERT_TRUE(strlen(buf) > 0U);
}

static void test_spawn_short_lived_process(void)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
    char exe[768];
    const char *sr = getenv("SystemRoot");
    const char *argv[2];

    TEST_ASSERT_NOT_NULL(sr);
    (void)snprintf(exe, sizeof exe, "%s\\System32\\hostname.exe", sr);
    argv[0] = exe;
    argv[1] = NULL;
#else
    const char *exe = "/bin/sh";
    const char *argv[4] = { "/bin/sh", "-c", "exit 0", NULL };
#endif
    int pid = 0;
    int code = -1;

#if defined(_WIN32) && !defined(__CYGWIN__)
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_process_spawn(exe, (const char *const *)argv, &pid));
#else
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_process_spawn(exe, argv, &pid));
#endif
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_process_wait_for_pid_exit(pid, &code));
    TEST_ASSERT_EQUAL_INT(0, code);
}

void test_group_platform(void)
{
    RUN_TEST(test_get_executable_path_non_empty);
    RUN_TEST(test_system_temp_dir);
    RUN_TEST(test_spawn_short_lived_process);
}
