#include "unity.h"
#include "test_helpers.h"

#include <stdio.h>
#include <string.h>

#include "platform_fs.h"
#include <update.h>

static void write_text(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fputs(text, fp);
    fclose(fp);
}

static void test_merge_overlay_install_keeps_unlisted_files(void)
{
    char inst[768];
    char bak[768];
    char stg[768];
    char path_keep_bak[768];
    char path_zip_stg[768];
    char path_keep_inst[768];
    char path_zip_inst[768];
    char buf[128];

    TEST_ASSERT_NOT_NULL(test_env_root());
    TEST_ASSERT_EQUAL_INT(0, test_env_join(inst, sizeof inst, "mrg_inst"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(bak, sizeof bak, "mrg_bak"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(stg, sizeof stg, "mrg_stg"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(path_keep_bak, sizeof path_keep_bak, "mrg_bak/keep.dat"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(path_zip_stg, sizeof path_zip_stg, "mrg_stg/from_zip.txt"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(path_keep_inst, sizeof path_keep_inst, "mrg_inst/keep.dat"));
    TEST_ASSERT_EQUAL_INT(0, test_env_join(path_zip_inst, sizeof path_zip_inst, "mrg_inst/from_zip.txt"));

    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_create_directory_recursive(bak));
    TEST_ASSERT_EQUAL_INT(PLATFORM_OK, platform_fs_create_directory_recursive(stg));
    write_text(path_keep_bak, "preserve me\n");
    write_text(path_zip_stg, "updated\n");

    TEST_ASSERT_EQUAL_INT(UPDATE_OK, update_merge_overlay_install(inst, bak, stg));

    {
        FILE *fp = fopen(path_keep_inst, "rb");
        TEST_ASSERT_NOT_NULL(fp);
        TEST_ASSERT_NOT_NULL(fgets(buf, (int)sizeof buf, fp));
        fclose(fp);
        TEST_ASSERT_EQUAL_STRING("preserve me\n", buf);
    }

    {
        FILE *fp = fopen(path_zip_inst, "rb");
        TEST_ASSERT_NOT_NULL(fp);
        TEST_ASSERT_NOT_NULL(fgets(buf, (int)sizeof buf, fp));
        fclose(fp);
        TEST_ASSERT_EQUAL_STRING("updated\n", buf);
    }
}

void test_group_merge_overlay(void)
{
    RUN_TEST(test_merge_overlay_install_keeps_unlisted_files);
}
