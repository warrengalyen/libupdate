#include "unity.h"
#include "test_helpers.h"
#include <stdlib.h>
#include <string.h>

#include <update.h>

#include "platform_fs.h"

#include "update_test_access.h"

static void test_init_valid_required_fields(void)
{
    update_options_t o;
    char inst[512];
    char tmp[512];

    TEST_ASSERT_NOT_NULL(test_env_root());
    TEST_ASSERT_EQUAL_INT(0, test_env_join(inst, sizeof inst, "install"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(tmp, sizeof tmp, "tmp"));
    (void)update_remove_tree(inst);
    (void)update_remove_tree(tmp);
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_create_directory_recursive(inst));
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_create_directory_recursive(tmp));

    memset(&o, 0, sizeof(o));
    o.update_url = "http://127.0.0.1:1/update.json";
    o.app_name = "unit";
    o.install_dir = inst;
    o.temp_dir = tmp;

    TEST_ASSERT_EQUAL_INT(UPDATE_OK, update_init(&o));
    TEST_ASSERT_EQUAL_STRING("http://127.0.0.1:1/update.json", update_test_get_update_url());
    TEST_ASSERT_EQUAL_STRING(inst, update_test_get_install_dir());
}

static void test_init_missing_url_fails(void)
{
    update_options_t o;

    memset(&o, 0, sizeof(o));
    o.app_name = "unit";
    TEST_ASSERT_EQUAL_INT(UPDATE_ERROR, update_init(&o));
}

static void test_init_missing_app_fails(void)
{
    update_options_t o;

    memset(&o, 0, sizeof(o));
    o.update_url = "http://127.0.0.1:1/x";
    TEST_ASSERT_EQUAL_INT(UPDATE_ERROR, update_init(&o));
}

static void test_double_init_fails(void)
{
    update_options_t o;

    memset(&o, 0, sizeof(o));
    o.update_url = "http://127.0.0.1:1/a";
    o.app_name = "unit";
    TEST_ASSERT_EQUAL_INT(UPDATE_OK, update_init(&o));
    TEST_ASSERT_EQUAL_INT(UPDATE_ERROR, update_init(&o));
}

static void test_strings_copied_not_aliased(void)
{
    update_options_t o;
    char *url;
    char *app;
    char *inst;
    char ibuf[512];
    char tbuf[512];

    TEST_ASSERT_NOT_NULL(test_env_root());
    TEST_ASSERT_EQUAL_INT(0, test_env_join(ibuf, sizeof ibuf, "install"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(tbuf, sizeof tbuf, "tmp"));
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_create_directory_recursive(ibuf));
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_create_directory_recursive(tbuf));

    url = (char *)malloc(64);
    app = (char *)malloc(64);
    inst = (char *)malloc(512);
    TEST_ASSERT_NOT_NULL(url);
    TEST_ASSERT_NOT_NULL(app);
    TEST_ASSERT_NOT_NULL(inst);

    strcpy(url, "http://127.0.0.1:1/manifest.json");
    strcpy(app, "aliased");
    strcpy(inst, ibuf);

    memset(&o, 0, sizeof(o));
    o.update_url = url;
    o.app_name = app;
    o.install_dir = inst;
    o.temp_dir = tbuf;

    TEST_ASSERT_EQUAL_INT(UPDATE_OK, update_init(&o));

    memset(url, 'x', 64);
    memset(app, 'y', 64);
    memset(inst, 'z', 512);
    free(url);
    free(app);
    free(inst);

    TEST_ASSERT_EQUAL_STRING("http://127.0.0.1:1/manifest.json", update_test_get_update_url());
    TEST_ASSERT_EQUAL_STRING(ibuf, update_test_get_install_dir());
}

void test_group_update_init(void)
{
    RUN_TEST(test_init_valid_required_fields);
    RUN_TEST(test_init_missing_url_fails);
    RUN_TEST(test_init_missing_app_fails);
    RUN_TEST(test_double_init_fails);
    RUN_TEST(test_strings_copied_not_aliased);
}
