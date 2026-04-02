#include "update_remote_check.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UPDATE_APP_VERSION_STRING
    #define UPDATE_APP_VERSION_STRING "0.0.0"
#endif

static const char *skip_ws(const char *p)
{
    while (p != NULL && *p != '\0' && isspace((unsigned char)*p) != 0) {
        p++;
    }
    return p;
}

static int json_extract_string(const char *json, const char *key, char *out, size_t outcap)
{
    char needle[96];
    size_t kl = strlen(key);
    const char *p;
    size_t i;
    int n;

    if (kl + 3U > sizeof(needle)) {
        return -1;
    }

    n = snprintf(needle, sizeof needle, "\"%s\"", key);
    if (n < 0 || (size_t)n >= sizeof needle) {
        return -1;
    }

    p = strstr(json, needle);
    if (p == NULL) {
        return -1;
    }

    p += (size_t)n;
    p = skip_ws(p);
    if (*p != ':') {
        return -1;
    }

    p = skip_ws(p + 1);
    if (*p != '"') {
        return -1;
    }

    p++;
    i = 0U;
    while (*p != '\0') {
        if (*p == '"') {
            break;
        }
        if (*p == '\\' && p[1] != '\0') {
            p++;
            switch (*p) {
            case '"':
            case '\\':
            case '/':
                break;
            case 'n':
                if (i + 1U >= outcap) {
                    return -1;
                }
                out[i++] = '\n';
                p++;
                continue;
            case 'r':
                if (i + 1U >= outcap) {
                    return -1;
                }
                out[i++] = '\r';
                p++;
                continue;
            case 't':
                if (i + 1U >= outcap) {
                    return -1;
                }
                out[i++] = '\t';
                p++;
                continue;
            default:
                if (i + 1U >= outcap) {
                    return -1;
                }
                out[i++] = *p;
                p++;
                continue;
            }
            if (i + 1U >= outcap) {
                return -1;
            }
            out[i++] = *p++;
            continue;
        }

        if (i + 1U >= outcap) {
            return -1;
        }
        out[i++] = *p++;
    }

    if (*p != '"') {
        return -1;
    }

    out[i] = '\0';
    return 0;
}

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

    p = skip_ws(p);
    if (*p == 'v' || *p == 'V') {
        p++;
    }

    n = sscanf(p, "%u.%u.%u", ma, mi, pa);
    return n >= 1 ? 0 : -1;
}

/**
 * Returns 1 if remote is strictly newer than local, 0 if remote <= local, -1 if remote invalid.
 */
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

int update_remote_check(const char *url,
    update_http_progress_fn on_progress,
    void *progress_user,
    update_info_t *out)
{
    char *body = NULL;
    size_t blen = 0U;
    char ver[256];
    char durl[768];
    char cks[96];
    int newer;

    if (url == NULL || url[0] == '\0' || out == NULL) {
        return UPDATE_ERROR;
    }

    memset(out, 0, sizeof(*out));

    if (update_http_fetch(url, &body, &blen, on_progress, progress_user) != 0) {
        return UPDATE_ERROR;
    }

    if (json_extract_string(body, "version", ver, sizeof ver) != 0
        || json_extract_string(body, "download_url", durl, sizeof durl) != 0
        || json_extract_string(body, "checksum", cks, sizeof cks) != 0) {
        free(body);
        return UPDATE_ERROR;
    }

    free(body);
    body = NULL;

    if (copy_info_field(out->version, sizeof out->version, ver) != 0
        || copy_info_field(out->download_url, sizeof out->download_url, durl) != 0
        || copy_info_field(out->checksum, sizeof out->checksum, cks) != 0) {
        return UPDATE_ERROR;
    }

    newer = remote_version_is_newer(ver, UPDATE_APP_VERSION_STRING);
    if (newer < 0) {
        return UPDATE_ERROR;
    }

    if (newer == 0) {
        return UPDATE_NOT_AVAILABLE;
    }

    return UPDATE_AVAILABLE;
}
