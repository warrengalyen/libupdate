#include "unity.h"
#include "test_helpers.h"

#include <stdlib.h>
#include <string.h>

#include "update.h"

static void rm_manifest(const char *json, update_info_t *info, int expect_ok)
{
    int rc;

    rc = update_manifest_parse(json, info);
    if (expect_ok != 0) {
        TEST_ASSERT_EQUAL_INT(0, rc);
    } else {
        TEST_ASSERT_NOT_EQUAL(0, rc);
    }
}

static void test_manifest_plaintext_escapes_html(void)
{
    update_info_t info;
    const char *j =
        "{ \"version\": \"2.0.0\", \"download_url\": \"https://x/a.zip\", \"checksum\": \""
        "0000000000000000000000000000000000000000000000000000000000000000"
        "\", \"description\": \"a<b>c\" }";

    rm_manifest(j, &info, 1);
    TEST_ASSERT_EQUAL_STRING("plaintext", update_get_description_format(&info));
    TEST_ASSERT_EQUAL(0, update_description_is_html(&info));
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "&lt;") != NULL);
    update_info_free(&info);
}

static void test_manifest_html_bold_list(void)
{
    update_info_t info;
    const char *j =
        "{ \"version\": \"3.0.0\", \"download_url\": \"https://x/a.zip\", \"checksum\": \""
        "0000000000000000000000000000000000000000000000000000000000000000"
        "\", \"description_format\": \"html\", \"description\": \"<b>Hi</b><ul><li>one</li></ul>\" }";

    rm_manifest(j, &info, 1);
    TEST_ASSERT_EQUAL_STRING("html", update_get_description_format(&info));
    TEST_ASSERT_NOT_EQUAL(0, update_description_is_html(&info));
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "<b>") != NULL);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "<ul>") != NULL);
    update_info_free(&info);
}

static void test_html_script_stripped_with_content(void)
{
    update_info_t info;
    const char *j =
        "{ \"version\": \"3.0.0\", \"download_url\": \"https://x/a.zip\", \"checksum\": \""
        "0000000000000000000000000000000000000000000000000000000000000000"
        "\", \"description_format\": \"html\", \"description\": \"pre<script>bad()</script>post\" }";

    rm_manifest(j, &info, 1);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "pre") != NULL);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "post") != NULL);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "script") == NULL);
    update_info_free(&info);
}

static void test_html_unknown_tag_unwrapped(void)
{
    update_info_t info;
    const char *j =
        "{ \"version\": \"3.0.0\", \"download_url\": \"https://x/a.zip\", \"checksum\": \""
        "0000000000000000000000000000000000000000000000000000000000000000"
        "\", \"description_format\": \"html\", \"description\": \"<div><b>x</b></div>\" }";

    rm_manifest(j, &info, 1);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "<b>") != NULL);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "div") == NULL);
    update_info_free(&info);
}

static void test_html_malformed_lone_bracket(void)
{
    update_info_t info;
    const char *j =
        "{ \"version\": \"3.0.0\", \"download_url\": \"https://x/a.zip\", \"checksum\": \""
        "0000000000000000000000000000000000000000000000000000000000000000"
        "\", \"description_format\": \"html\", \"description\": \"a <<b>c</b>\" }";

    rm_manifest(j, &info, 1);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "&lt;") != NULL);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "<b>") != NULL);
    update_info_free(&info);
}

static void test_unknown_format_is_plaintext(void)
{
    update_info_t info;
    const char *j =
        "{ \"version\": \"3.0.0\", \"download_url\": \"https://x/a.zip\", \"checksum\": \""
        "0000000000000000000000000000000000000000000000000000000000000000"
        "\", \"description_format\": \"markdown\", \"description\": \"*hi*\" }";

    rm_manifest(j, &info, 1);
    TEST_ASSERT_EQUAL_STRING("plaintext", update_get_description_format(&info));
    update_info_free(&info);
}

static void test_empty_description_key_missing(void)
{
    update_info_t info;
    const char *j = "{ \"version\": \"3.0.0\", \"download_url\": \"https://x/a.zip\", \"checksum\": \""
                    "0000000000000000000000000000000000000000000000000000000000000000"
                    "\" }";

    rm_manifest(j, &info, 1);
    TEST_ASSERT_NULL(info.description);
    TEST_ASSERT_EQUAL_STRING("", update_get_description(&info));
    update_info_free(&info);
}

static void test_empty_description_string(void)
{
    update_info_t info;
    const char *j =
        "{ \"version\": \"3.0.0\", \"download_url\": \"https://x/a.zip\", \"checksum\": \""
        "0000000000000000000000000000000000000000000000000000000000000000"
        "\", \"description\": \"\" }";

    rm_manifest(j, &info, 1);
    TEST_ASSERT_NOT_NULL(info.description);
    TEST_ASSERT_EQUAL_STRING("", update_get_description(&info));
    update_info_free(&info);
}

static void test_javascript_href_removed(void)
{
    update_info_t info;
    const char *j =
        "{ \"version\": \"3.0.0\", \"download_url\": \"https://x/a.zip\", \"checksum\": \""
        "0000000000000000000000000000000000000000000000000000000000000000"
        "\", \"description_format\": \"html\", \"description\": "
        "\"<a href=\\\"javascript:alert(1)\\\">x</a>\" }";

    rm_manifest(j, &info, 1);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "javascript:") == NULL);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "<a ") == NULL);
    update_info_free(&info);
}

static void test_safe_https_href_kept(void)
{
    update_info_t info;
    const char *j =
        "{ \"version\": \"3.0.0\", \"download_url\": \"https://x/a.zip\", \"checksum\": \""
        "0000000000000000000000000000000000000000000000000000000000000000"
        "\", \"description_format\": \"html\", \"description\": "
        "\"<a href=\\\"https://example.com/x\\\">x</a>\" }";

    rm_manifest(j, &info, 1);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "https://example.com/x") != NULL);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "<a href=") != NULL);
    update_info_free(&info);
}

static void test_onclick_stripped(void)
{
    update_info_t info;
    const char *j =
        "{ \"version\": \"3.0.0\", \"download_url\": \"https://x/a.zip\", \"checksum\": \""
        "0000000000000000000000000000000000000000000000000000000000000000"
        "\", \"description_format\": \"html\", \"description\": "
        "\"<b onclick=\\\"alert(1)\\\">x</b>\" }";

    rm_manifest(j, &info, 1);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "onclick") == NULL);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "<b>") != NULL);
    update_info_free(&info);
}

static void test_nested_invalid_tags(void)
{
    update_info_t info;
    const char *j =
        "{ \"version\": \"3.0.0\", \"download_url\": \"https://x/a.zip\", \"checksum\": \""
        "0000000000000000000000000000000000000000000000000000000000000000"
        "\", \"description_format\": \"html\", \"description\": "
        "\"<p><script><b>x</b></script></p>\" }";

    rm_manifest(j, &info, 1);
    TEST_ASSERT_TRUE(strstr(update_get_description(&info), "script") == NULL);
    update_info_free(&info);
}

static void test_large_plaintext_bounded(void)
{
    update_info_t info;
    char *buf;
    size_t bl = 64U * 1024U + 256U;
    size_t json_cap = bl + 512U;
    char *json;
    size_t p;

    buf = (char *)malloc(bl + 1U);
    json = (char *)malloc(json_cap);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_NOT_NULL(json);
    memset(buf, 'A', bl);
    buf[bl] = '\0';

    p = (size_t)snprintf(json, json_cap,
        "{ \"version\": \"3.0.0\", \"download_url\": \"https://x/a.zip\", \"checksum\": \""
        "0000000000000000000000000000000000000000000000000000000000000000"
        "\", \"description\": \"");
    TEST_ASSERT_TRUE(p < json_cap);
    TEST_ASSERT_TRUE(p + bl + 8U < json_cap);
    memcpy(json + p, buf, bl);
    p += bl;
    (void)memcpy(json + p, "\" }", 4U);
    json[p + 3U] = '\0';

    rm_manifest(json, &info, 1);
    TEST_ASSERT_NOT_NULL(info.description);
    TEST_ASSERT_EQUAL_UINT((unsigned)bl, (unsigned)strlen(info.description));
    update_info_free(&info);
    free(json);
    free(buf);
}

static void test_manifest_missing_required_key(void)
{
    update_info_t info;
    const char *j = "{ \"version\": \"1.0.0\" }";

    rm_manifest(j, &info, 0);
}

void test_group_release_notes(void)
{
    RUN_TEST(test_manifest_missing_required_key);
    RUN_TEST(test_manifest_plaintext_escapes_html);
    RUN_TEST(test_manifest_html_bold_list);
    RUN_TEST(test_html_script_stripped_with_content);
    RUN_TEST(test_html_unknown_tag_unwrapped);
    RUN_TEST(test_html_malformed_lone_bracket);
    RUN_TEST(test_unknown_format_is_plaintext);
    RUN_TEST(test_empty_description_key_missing);
    RUN_TEST(test_empty_description_string);
    RUN_TEST(test_javascript_href_removed);
    RUN_TEST(test_safe_https_href_kept);
    RUN_TEST(test_onclick_stripped);
    RUN_TEST(test_nested_invalid_tags);
    RUN_TEST(test_large_plaintext_bounded);
}
