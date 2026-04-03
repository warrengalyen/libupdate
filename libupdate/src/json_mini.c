#include "json_mini.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

const char *json_mini_skip_ws(const char *p)
{
    while (p != NULL && *p != '\0' && isspace((unsigned char)*p) != 0) {
        p++;
    }
    return p;
}

int json_mini_extract_string(const char *json, const char *key, char *out, size_t outcap)
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
    p = json_mini_skip_ws(p);
    if (*p != ':') {
        return -1;
    }

    p = json_mini_skip_ws(p + 1);
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
