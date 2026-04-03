#include "unity.h"
#include "test_helpers.h"
#include "test_config.h"

#include <stdio.h>
#include <string.h>

#include <update.h>

static void test_validate_path_rejects_dotdot(void)
{
    TEST_ASSERT_EQUAL_INT(UPDATE_ERROR, update_validate_path("C:\\temp\\..\\evil", 0U));
    TEST_ASSERT_EQUAL_INT(UPDATE_ERROR, update_validate_path("/tmp/../etc", 0U));
}

static void test_extract_minimal_and_nested(void)
{
    char dest[768];

    TEST_ASSERT_NOT_NULL(test_env_root());
    TEST_ASSERT_EQUAL_INT(0, test_env_join(dest, sizeof dest, "extract_out"));
    (void)update_remove_tree(dest);
    TEST_ASSERT_EQUAL_INT(UPDATE_OK, update_extract(LIBUPDATE_TEST_MINIMAL_ZIP, dest));

    {
        char p[768];
        char line[64];
        FILE *fp;

        TEST_ASSERT_EQUAL_INT(0, test_env_join(p, sizeof p, "extract_out/hello.txt"));
        fp = fopen(p, "rb");
        TEST_ASSERT_NOT_NULL(fp);
        TEST_ASSERT_NOT_NULL(fgets(line, sizeof line, fp));
        fclose(fp);
        TEST_ASSERT_EQUAL_INT(0, strncmp(line, "hello", 5));

        TEST_ASSERT_EQUAL_INT(0, test_env_join(p, sizeof p, "extract_out/nested/a.txt"));
        fp = fopen(p, "rb");
        TEST_ASSERT_NOT_NULL(fp);
        TEST_ASSERT_NOT_NULL(fgets(line, sizeof line, fp));
        fclose(fp);
        TEST_ASSERT_EQUAL_INT(0, strncmp(line, "nested", 6));
    }
}

static void test_extract_corrupt_archive_fails(void)
{
    char zip_path[768];
    char dest[768];
    FILE *fp;

    TEST_ASSERT_NOT_NULL(test_env_root());
    TEST_ASSERT_EQUAL_INT(0, test_env_join(zip_path, sizeof zip_path, "not_a_zip.zip"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(dest, sizeof dest, "extract_bad"));
    fp = fopen(zip_path, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fputs("this is not a zip file", fp);
    fclose(fp);
    (void)update_remove_tree(dest);
    TEST_ASSERT_EQUAL_INT(UPDATE_ERROR, update_extract(zip_path, dest));
}

static void test_extract_overwrite_second_run(void)
{
    char dest[768];

    TEST_ASSERT_NOT_NULL(test_env_root());
    TEST_ASSERT_EQUAL_INT(0, test_env_join(dest, sizeof dest, "extract_twice"));
    (void)update_remove_tree(dest);
    TEST_ASSERT_EQUAL_INT(UPDATE_OK, update_extract(LIBUPDATE_TEST_MINIMAL_ZIP, dest));
    TEST_ASSERT_EQUAL_INT(UPDATE_OK, update_extract(LIBUPDATE_TEST_MINIMAL_ZIP, dest));
}

#if LIBUPDATE_HAVE_SLIP_ZIP
static void test_extract_rejects_zip_slip(void)
{
    char dest[768];

    TEST_ASSERT_NOT_NULL(test_env_root());
    TEST_ASSERT_EQUAL_INT(0, test_env_join(dest, sizeof dest, "extract_slip"));
    (void)update_remove_tree(dest);
    TEST_ASSERT_EQUAL_INT(UPDATE_ERROR, update_extract(LIBUPDATE_TEST_SLIP_ZIP, dest));
}
#endif

void test_group_extract(void)
{
    RUN_TEST(test_validate_path_rejects_dotdot);
    RUN_TEST(test_extract_minimal_and_nested);
    RUN_TEST(test_extract_corrupt_archive_fails);
    RUN_TEST(test_extract_overwrite_second_run);
#if LIBUPDATE_HAVE_SLIP_ZIP
    RUN_TEST(test_extract_rejects_zip_slip);
#endif
}
