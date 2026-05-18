#include "json_mini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *json_mini_skip_ws(const char *p)
{
    while (p != NULL && *p != '\0' && isspace((unsigned char)*p) != 0) {
        p++;
    }
    return p;
}

static const char *find_string_value_start(const char *json, const char *key)
{
    char needle[96];
    size_t kl = strlen(key);
    const char *p;
    int n;

    if (json == NULL || key == NULL || kl + 3U > sizeof(needle)) {
        return NULL;
    }

    n = snprintf(needle, sizeof needle, "\"%s\"", key);
    if (n < 0 || (size_t)n >= sizeof needle) {
        return NULL;
    }

    p = strstr(json, needle);
    if (p == NULL) {
        return NULL;
    }

    p += (size_t)n;
    p = json_mini_skip_ws(p);
    if (*p != ':') {
        return NULL;
    }

    p = json_mini_skip_ws(p + 1);
    if (*p != '"') {
        return NULL;
    }

    return p + 1;
}

/*
 * p points at first character inside JSON string value (after opening quote).
 * Writes decoded UTF-8 bytes to out (if non-NULL) up to outcap-1, NUL-terminated.
 * Sets *decoded_count to decoded byte count excluding NUL.
 * Sets *end_ptr to closing '"' or NULL on error.
 * max_decoded: reject if decoded length would exceed this (pass SIZE_MAX to disable).
 */
static int parse_json_string_value(const char *p,
    char *out,
    size_t outcap,
    size_t *decoded_count,
    const char **end_ptr,
    size_t max_decoded)
{
    size_t i = 0U;

    if (p == NULL || decoded_count == NULL || end_ptr == NULL) {
        return -1;
    }

    *end_ptr = NULL;
    *decoded_count = 0U;

    while (*p != '\0') {
        if (*p == '"') {
            if (out != NULL) {
                if (i >= outcap) {
                    return -1;
                }
                out[i] = '\0';
            }
            *decoded_count = i;
            *end_ptr = p;
            return 0;
        }
        if (max_decoded != (size_t)-1 && i >= max_decoded) {
            return -1;
        }
        if (*p == '\\' && p[1] != '\0') {
            p++;
            switch (*p) {
            case '"':
            case '\\':
            case '/':
                break;
            case 'n':
                if (out != NULL) {
                    if (i + 1U >= outcap) {
                        return -1;
                    }
                    out[i] = '\n';
                }
                i++;
                p++;
                continue;
            case 'r':
                if (out != NULL) {
                    if (i + 1U >= outcap) {
                        return -1;
                    }
                    out[i] = '\r';
                }
                i++;
                p++;
                continue;
            case 't':
                if (out != NULL) {
                    if (i + 1U >= outcap) {
                        return -1;
                    }
                    out[i] = '\t';
                }
                i++;
                p++;
                continue;
            default:
                break;
            }
            if (out != NULL) {
                if (i + 1U >= outcap) {
                    return -1;
                }
                out[i] = *p;
            }
            i++;
            p++;
            continue;
        }

        if (out != NULL) {
            if (i + 1U >= outcap) {
                return -1;
            }
            out[i] = *p;
        }
        i++;
        p++;
    }

    return -1;
}

int json_mini_extract_string(const char *json, const char *key, char *out, size_t outcap)
{
    const char *p;
    size_t dc;
    const char *endq;

    if (out == NULL || outcap == 0U) {
        return -1;
    }

    p = find_string_value_start(json, key);
    if (p == NULL) {
        return -1;
    }

    if (parse_json_string_value(p, out, outcap, &dc, &endq, (size_t)-1) != 0 || endq == NULL) {
        return -1;
    }

    (void)dc;
    return 0;
}

int json_mini_extract_string_opt(const char *json, const char *key, char *out, size_t outcap, int *present_out)
{
    const char *p;
    size_t dc;
    const char *endq;

    if (present_out == NULL) {
        return -1;
    }
    *present_out = 0;

    if (out != NULL && outcap > 0U) {
        out[0] = '\0';
    }

    p = find_string_value_start(json, key);
    if (p == NULL) {
        *present_out = 0;
        return 0;
    }

    *present_out = 1;

    if (out == NULL || outcap == 0U) {
        return -1;
    }

    if (parse_json_string_value(p, out, outcap, &dc, &endq, (size_t)-1) != 0 || endq == NULL) {
        return -1;
    }

    (void)dc;
    return 0;
}

int json_mini_extract_string_alloc(const char *json, const char *key, size_t max_value_len, char **out_alloc)
{
    const char *p;
    size_t dc;
    const char *endq;
    char *buf;

    if (out_alloc == NULL) {
        return -1;
    }
    *out_alloc = NULL;

    p = find_string_value_start(json, key);
    if (p == NULL) {
        return 0;
    }

    if (parse_json_string_value(p, NULL, 0U, &dc, &endq, max_value_len) != 0 || endq == NULL) {
        return -1;
    }

    buf = (char *)malloc(dc + 1U);
    if (buf == NULL) {
        return -1;
    }

    if (parse_json_string_value(p, buf, dc + 1U, &dc, &endq, max_value_len) != 0 || endq == NULL) {
        free(buf);
        return -1;
    }

    *out_alloc = buf;
    return 0;
}
