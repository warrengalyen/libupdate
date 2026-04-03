#include "unity.h"
#include "test_helpers.h"

#include "update_test_access.h"

void setUp(void)
{
    update_test_reset_context();
    test_env_setup();
}

void tearDown(void)
{
    test_env_cleanup();
    update_test_reset_context();
}

extern void test_group_update_init(void);
extern void test_group_verify(void);
extern void test_group_extract(void);
extern void test_group_download(void);
extern void test_group_apply(void);
extern void test_group_updater(void);
extern void test_group_platform(void);
extern void test_group_stress(void);

int main(void)
{
    UNITY_BEGIN();

    test_group_update_init();
    test_group_verify();
    test_group_extract();
    test_group_download();
    test_group_apply();
    test_group_updater();
    test_group_platform();
    test_group_stress();

    return UNITY_END();
}
