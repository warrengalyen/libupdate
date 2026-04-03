#include "unity.h"
#include "test_helpers.h"

#include <string.h>

#include "platform_process.h"

static void test_updater_usage_on_bad_args(void)
{
    char updater_path[768];
    const char *argv_u[3];
    int pid = 0;
    int code = 0;

#if defined(_WIN32) && !defined(__CYGWIN__)
    TEST_ASSERT_EQUAL_INT(0, test_bin_dir_join(updater_path, sizeof updater_path, "updater.exe"));
#else
    TEST_ASSERT_EQUAL_INT(0, test_bin_dir_join(updater_path, sizeof updater_path, "updater"));
#endif

    argv_u[0] = updater_path;
    argv_u[1] = "--invalid-flag";
    argv_u[2] = NULL;

    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_process_spawn(updater_path, (const char *const *)argv_u, &pid));
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_process_wait_for_pid_exit(pid, &code));
    TEST_ASSERT_NOT_EQUAL_INT(0, code);
}

void test_group_updater(void)
{
    RUN_TEST(test_updater_usage_on_bad_args);
}
