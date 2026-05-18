#include "update_remote_check.h"
#include "json_mini.h"
#include "update_html.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UPDATE_APP_VERSION_STRING
    #define UPDATE_APP_VERSION_STRING "0.0.0"
#endif

static int parse_semver3(const char *s, unsigned *ma, unsigned *mi, unsigned *pa)
{
    const char *p = s;
    int n;

    if (s == NULL) {
        return -1;
    }

    *ma = 0U;
    *mi = 0U;
    *pa = 0U;

    p = json_mini_skip_ws(p);
    if (*p == 'v' || *p == 'V') {
        p++;
    }

    n = sscanf(p, "%u.%u.%u", ma, mi, pa);
    return n >= 1 ? 0 : -1;
}

/* 1 = remote newer, 0 = remote <= local, -1 = invalid remote */
static int remote_version_is_newer(const char *remote, const char *local)
{
    unsigned rma, rmi, rpa;
    unsigned lma, lmi, lpa;

    if (parse_semver3(remote, &rma, &rmi, &rpa) != 0) {
        return -1;
    }

    if (parse_semver3(local, &lma, &lmi, &lpa) != 0) {
        lma = 0U;
        lmi = 0U;
        lpa = 0U;
    }

    if (rma > lma) {
        return 1;
    }
    if (rma < lma) {
        return 0;
    }
    if (rmi > lmi) {
        return 1;
    }
    if (rmi < lmi) {
        return 0;
    }
    if (rpa > lpa) {
        return 1;
    }
    return 0;
}

static int copy_info_field(char *dst, size_t cap, const char *src)
{
    size_t L;

    if (dst == NULL || cap == 0U || src == NULL) {
        return -1;
    }

    L = strlen(src);
    if (L + 1U > cap) {
        return -1;
    }

    memcpy(dst, src, L + 1U);
    return 0;
}

static unsigned char ascii_lower_uc(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (unsigned char)(c - 'A' + 'a');
    }
    return c;
}

static int format_token_is_html(const char *s)
{
    const char lit[] = "html";
    size_t i;

    if (s == NULL) {
        return 0;
    }

    for (i = 0; lit[i] != '\0'; i++) {
        if (ascii_lower_uc((unsigned char)s[i]) != (unsigned char)lit[i]) {
            return 0;
        }
    }
    return s[i] == '\0' ? 1 : 0;
}

UPDATE_API int update_manifest_parse(const char *body, update_info_t *out)
{
    char ver[256];
    char durl[512];
    char cks[96];
    char dfmt_raw[48];
    int dfmt_pres = 0;
    char *desc_raw = NULL;
    int want_html = 0;

    if (body == NULL || out == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    if (json_mini_extract_string(body, "version", ver, sizeof ver) != 0
        || json_mini_extract_string(body, "download_url", durl, sizeof durl) != 0
        || json_mini_extract_string(body, "checksum", cks, sizeof cks) != 0) {
        return -1;
    }

    if (json_mini_extract_string_opt(body, "description_format", dfmt_raw, sizeof dfmt_raw, &dfmt_pres) != 0) {
        return -1;
    }

    if (json_mini_extract_string_alloc(body, "description", UPDATE_HTML_DESC_MAX_IN, &desc_raw) != 0) {
        return -1;
    }

    if (copy_info_field(out->version, sizeof out->version, ver) != 0
        || copy_info_field(out->download_url, sizeof out->download_url, durl) != 0
        || copy_info_field(out->checksum, sizeof out->checksum, cks) != 0) {
        free(desc_raw);
        update_info_free(out);
        return -1;
    }

    if (dfmt_pres != 0 && format_token_is_html(dfmt_raw)) {
        want_html = 1;
    } else {
        want_html = 0;
    }

    if (want_html != 0) {
        if (snprintf(out->description_format, sizeof out->description_format, "html") >= (int)sizeof out->description_format) {
            free(desc_raw);
            update_info_free(out);
            return -1;
        }
    } else {
        if (snprintf(out->description_format, sizeof out->description_format, "plaintext")
            >= (int)sizeof out->description_format) {
            free(desc_raw);
            update_info_free(out);
            return -1;
        }
    }

    if (desc_raw == NULL) {
        return 0;
    }

    if (want_html != 0) {
        if (update_html_sanitize_alloc(desc_raw, strlen(desc_raw), &out->description, UPDATE_HTML_DESC_MAX_OUT) != 0) {
            free(desc_raw);
            update_info_free(out);
            return -1;
        }
    } else {
        if (update_html_escape_plain_alloc(desc_raw, strlen(desc_raw), &out->description, UPDATE_HTML_DESC_MAX_OUT) != 0) {
            free(desc_raw);
            update_info_free(out);
            return -1;
        }
    }

    free(desc_raw);
    return 0;
}

int update_remote_check(const char *url,
    update_http_progress_fn on_progress,
    void *progress_user,
    update_info_t *out)
{
    char *body = NULL;
    size_t blen = 0U;
    int newer;

    if (url == NULL || url[0] == '\0' || out == NULL) {
        return UPDATE_ERROR;
    }

    memset(out, 0, sizeof(*out));

    if (update_http_fetch(url, &body, &blen, on_progress, progress_user) != 0) {
        return UPDATE_ERROR;
    }

    if (update_manifest_parse(body, out) != 0) {
        free(body);
        return UPDATE_ERROR;
    }

    free(body);
    body = NULL;

    newer = remote_version_is_newer(out->version, UPDATE_APP_VERSION_STRING);
    if (newer < 0) {
        update_info_free(out);
        memset(out, 0, sizeof(*out));
        return UPDATE_ERROR;
    }

    if (newer == 0) {
        return UPDATE_NOT_AVAILABLE;
    }

    return UPDATE_AVAILABLE;
}
