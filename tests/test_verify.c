#include "unity.h"
#include "test_helpers.h"
#include "test_config.h"

#include <stdio.h>
#include <string.h>

#include <update.h>

#include "sha256.h"

static void test_verify_correct_hash(void)
{
    TEST_ASSERT_EQUAL_INT(UPDATE_OK, update_verify(LIBUPDATE_TEST_MINIMAL_ZIP, LIBUPDATE_TEST_MINIMAL_SHA256));
}

static void test_verify_wrong_hash_fails(void)
{
    const char *bad = "0000000000000000000000000000000000000000000000000000000000000000";
    TEST_ASSERT_EQUAL_INT(UPDATE_ERROR, update_verify(LIBUPDATE_TEST_MINIMAL_ZIP, bad));
}

static void test_verify_large_file(void)
{
    char path[768];
    sha256_ctx ctx;
    unsigned char digest[32];
    unsigned char expect[32];
    FILE *fp;
    unsigned char block[4096];
    size_t i;
    const size_t total = 2U * 1024U * 1024U;
    size_t written = 0U;
    char hex[65];
    static const char *const hexd = "0123456789abcdef";

    TEST_ASSERT_NOT_NULL(test_env_root());
    TEST_ASSERT_EQUAL_INT(0, test_env_join(path, sizeof path, "big.bin"));

    fp = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    memset(block, 0xA5, sizeof block);
    while (written < total) {
        size_t n = sizeof block;
        if (total - written < n) {
            n = total - written;
        }
        TEST_ASSERT_EQUAL_UINT(n, fwrite(block, 1U, n, fp));
        written += n;
    }
    TEST_ASSERT_EQUAL_INT(0, fclose(fp));

    sha256_init(&ctx);
    fp = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    for (;;) {
        size_t n = fread(block, 1U, sizeof block, fp);
        if (n > 0U) {
            sha256_update(&ctx, block, n);
        }
        if (n < sizeof block) {
            break;
        }
    }
    TEST_ASSERT_EQUAL_INT(0, ferror(fp));
    TEST_ASSERT_EQUAL_INT(0, fclose(fp));
    sha256_final(&ctx, digest);

    for (i = 0U; i < 32U; i++) {
        hex[i * 2U] = hexd[digest[i] >> 4];
        hex[i * 2U + 1U] = hexd[digest[i] & 0x0FU];
    }
    hex[64] = '\0';

    TEST_ASSERT_EQUAL_INT(UPDATE_OK, update_verify(path, hex));
}

void test_group_verify(void)
{
    RUN_TEST(test_verify_correct_hash);
    RUN_TEST(test_verify_wrong_hash_fails);
    RUN_TEST(test_verify_large_file);
}
