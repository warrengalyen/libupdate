#include "unity.h"
#include "test_config.h"

#include <update.h>

static void test_repeated_verify(void)
{
    unsigned u;
    for (u = 0U; u < 32U; u++) {
        TEST_ASSERT_EQUAL_INT(UPDATE_OK, update_verify(LIBUPDATE_TEST_MINIMAL_ZIP, LIBUPDATE_TEST_MINIMAL_SHA256));
    }
}

void test_group_stress(void)
{
    RUN_TEST(test_repeated_verify);
}
