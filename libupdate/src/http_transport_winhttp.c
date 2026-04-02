#include "http_transport.h"
#include "platform_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <windows.h>
#include <winhttp.h>

static wchar_t *utf8_to_wide(const char *s)
{
    int n;
    wchar_t *w;

    if (s == NULL) {
        return NULL;
    }

    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) {
        return NULL;
    }

    w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (w == NULL) {
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n) <= 0) {
        free(w);
        return NULL;
    }

    return w;
}

static char *temp_path_for(const char *dest)
{
    static const char suf[] = ".download";
    size_t n;
    char *t;

    n = strlen(dest) + sizeof(suf);
    t = (char *)malloc(n);
    if (t == NULL) {
        return NULL;
    }
    memcpy(t, dest, strlen(dest) + 1U);
    (void)strcat(t, suf);
    return t;
}

static int winhttp_parse_url(const char *url,
    char *host,
    size_t hostcap,
    char *path,
    size_t pathcap,
    int *https,
    unsigned *port_out)
{
    const char *p;
    const char *pathstart;
    const char *host_end;
    const char *colon;
    size_t hostlen;

    if (strncmp(url, "https://", 8) == 0) {
        *https = 1;
        *port_out = 443U;
        p = url + 8U;
    } else if (strncmp(url, "http://", 7) == 0) {
        *https = 0;
        *port_out = 80U;
        p = url + 7U;
    } else {
        return -1;
    }

    pathstart = strchr(p, '/');
    if (pathstart == NULL) {
        host_end = p + strlen(p);
        if ((size_t)(host_end - p) + 1U > hostcap) {
            return -1;
        }
        memcpy(host, p, (size_t)(host_end - p));
        host[(size_t)(host_end - p)] = '\0';
        if (pathcap < 2U) {
            return -1;
        }
        path[0] = '/';
        path[1] = '\0';
        return 0;
    }

    host_end = pathstart;
    colon = memchr(p, ':', (size_t)(host_end - p));
    if (colon != NULL) {
        hostlen = (size_t)(colon - p);
        if (hostlen + 1U > hostcap) {
            return -1;
        }
        memcpy(host, p, hostlen);
        host[hostlen] = '\0';
        *port_out = (unsigned)strtoul(colon + 1U, NULL, 10);
        if (*port_out == 0U) {
            *port_out = *https ? 443U : 80U;
        }
    } else {
        hostlen = (size_t)(host_end - p);
        if (hostlen + 1U > hostcap) {
            return -1;
        }
        memcpy(host, p, hostlen);
        host[hostlen] = '\0';
    }

    {
        size_t plen = strlen(pathstart);
        if (plen + 1U > pathcap) {
            return -1;
        }
        memcpy(path, pathstart, plen + 1U);
    }

    return 0;
}

int update_http_stream_download(const char *url,
    const char *dest_path,
    update_http_progress_fn on_progress,
    void *progress_user)
{
    char hostb[512];
    char pathb[4096];
    wchar_t *whost = NULL;
    wchar_t *wreqpath = NULL;
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    DWORD flags = 0;
    INTERNET_PORT port;
    int https = 0;
    DWORD status = 0;
    DWORD status_size;
    unsigned long long total_hint = 0ULL;
    unsigned long long transferred = 0ULL;
    DWORD clen = 0;
    DWORD clen_size;
    FILE *out = NULL;
    char *tmp_path = NULL;
    int rc = -1;
    unsigned port_u = 80U;

    if (url == NULL || dest_path == NULL || dest_path[0] == '\0') {
        return -1;
    }

    tmp_path = temp_path_for(dest_path);
    if (tmp_path == NULL) {
        return -1;
    }

    if (winhttp_parse_url(url, hostb, sizeof(hostb), pathb, sizeof(pathb), &https, &port_u) != 0) {
        goto cleanup;
    }

    port = (INTERNET_PORT)port_u;

    if (https) {
        flags |= WINHTTP_FLAG_SECURE;
    }

    whost = utf8_to_wide(hostb);
    wreqpath = utf8_to_wide(pathb);
    if (whost == NULL || wreqpath == NULL) {
        goto cleanup;
    }

    hSession = WinHttpOpen(L"libupdate/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0U);
    if (hSession == NULL) {
        goto cleanup;
    }

    hConnect = WinHttpConnect(hSession, whost, port, 0U);
    if (hConnect == NULL) {
        goto cleanup;
    }

    hRequest = WinHttpOpenRequest(hConnect,
        L"GET",
        wreqpath,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (hRequest == NULL) {
        goto cleanup;
    }

    if (WinHttpSendRequest(hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0U,
            WINHTTP_NO_REQUEST_DATA,
            0U,
            0U,
            0U)
        == FALSE) {
        goto cleanup;
    }

    if (WinHttpReceiveResponse(hRequest, NULL) == FALSE) {
        goto cleanup;
    }

    status_size = sizeof(status);
    if (WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX)
        == FALSE) {
        goto cleanup;
    }

    if (status != 200U) {
        goto cleanup;
    }

    clen_size = sizeof(clen);
    if (WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &clen,
            &clen_size,
            WINHTTP_NO_HEADER_INDEX)
        != FALSE) {
        total_hint = (unsigned long long)clen;
    }

    out = fopen(tmp_path, "wb");
    if (out == NULL) {
        goto cleanup;
    }

    for (;;) {
        unsigned char buf[8192];
        DWORD rd = 0U;

        if (WinHttpReadData(hRequest, buf, sizeof(buf), &rd) == FALSE) {
            goto cleanup;
        }
        if (rd == 0U) {
            break;
        }

        if (fwrite(buf, 1U, (size_t)rd, out) != (size_t)rd) {
            goto cleanup;
        }

        transferred += (unsigned long long)rd;
        if (on_progress != NULL) {
            on_progress(transferred, total_hint, progress_user);
        }
    }

    if (fclose(out) != 0) {
        out = NULL;
        goto cleanup;
    }
    out = NULL;

    if (platform_fs_move_path(tmp_path, dest_path) != PLATFORM_OK) {
        goto cleanup;
    }

    rc = 0;

cleanup:
    if (out != NULL) {
        (void)fclose(out);
    }
    if (rc != 0) {
        (void)platform_fs_remove_path(tmp_path);
    }
    if (hRequest != NULL) {
        (void)WinHttpCloseHandle(hRequest);
    }
    if (hConnect != NULL) {
        (void)WinHttpCloseHandle(hConnect);
    }
    if (hSession != NULL) {
        (void)WinHttpCloseHandle(hSession);
    }
    free(whost);
    free(wreqpath);
    free(tmp_path);
    return rc;
}

#define UPDATE_HTTP_FETCH_MAX (512U * 1024U)

static int win_fetch_append(char **buf, size_t *len, size_t *cap, const void *chunk, size_t chunk_len)
{
    size_t need;

    need = *len + chunk_len + 1U;
    if (need > *cap) {
        size_t ncap = *cap == 0U ? 4096U : *cap * 2U;
        while (ncap < need) {
            ncap *= 2U;
        }
        {
            char *nb = (char *)realloc(*buf, ncap);
            if (nb == NULL) {
                return -1;
            }
            *buf = nb;
            *cap = ncap;
        }
    }

    memcpy(*buf + *len, chunk, chunk_len);
    *len += chunk_len;
    (*buf)[*len] = '\0';

    if (*len > UPDATE_HTTP_FETCH_MAX) {
        return -1;
    }

    return 0;
}

int update_http_fetch(const char *url,
    char **out_body,
    size_t *out_len,
    update_http_progress_fn on_progress,
    void *progress_user)
{
    char hostb[512];
    char pathb[4096];
    wchar_t *whost = NULL;
    wchar_t *wreqpath = NULL;
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    DWORD flags = 0;
    INTERNET_PORT port;
    int https = 0;
    DWORD status = 0;
    DWORD status_size;
    unsigned long long total_hint = 0ULL;
    unsigned long long transferred = 0ULL;
    DWORD clen = 0;
    DWORD clen_size;
    char *body = NULL;
    size_t blen = 0U;
    size_t bcap = 0U;
    int rc = -1;
    unsigned port_u = 80U;

    if (url == NULL || out_body == NULL || out_len == NULL) {
        return -1;
    }

    *out_body = NULL;
    *out_len = 0U;

    if (winhttp_parse_url(url, hostb, sizeof(hostb), pathb, sizeof(pathb), &https, &port_u) != 0) {
        return -1;
    }

    port = (INTERNET_PORT)port_u;

    if (https) {
        flags |= WINHTTP_FLAG_SECURE;
    }

    whost = utf8_to_wide(hostb);
    wreqpath = utf8_to_wide(pathb);
    if (whost == NULL || wreqpath == NULL) {
        goto cleanup;
    }

    hSession = WinHttpOpen(L"libupdate/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0U);
    if (hSession == NULL) {
        goto cleanup;
    }

    hConnect = WinHttpConnect(hSession, whost, port, 0U);
    if (hConnect == NULL) {
        goto cleanup;
    }

    hRequest = WinHttpOpenRequest(hConnect,
        L"GET",
        wreqpath,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (hRequest == NULL) {
        goto cleanup;
    }

    (void)WinHttpAddRequestHeaders(hRequest,
        L"Accept: application/json\r\n",
        (DWORD)-1L,
        WINHTTP_ADDREQ_FLAG_ADD);

    if (WinHttpSendRequest(hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0U,
            WINHTTP_NO_REQUEST_DATA,
            0U,
            0U,
            0U)
        == FALSE) {
        goto cleanup;
    }

    if (WinHttpReceiveResponse(hRequest, NULL) == FALSE) {
        goto cleanup;
    }

    status_size = sizeof(status);
    if (WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX)
        == FALSE) {
        goto cleanup;
    }

    if (status != 200U) {
        goto cleanup;
    }

    clen_size = sizeof(clen);
    if (WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &clen,
            &clen_size,
            WINHTTP_NO_HEADER_INDEX)
        != FALSE) {
        total_hint = (unsigned long long)clen;
    }

    for (;;) {
        unsigned char buf[8192];
        DWORD rd = 0U;

        if (WinHttpReadData(hRequest, buf, sizeof(buf), &rd) == FALSE) {
            goto cleanup;
        }
        if (rd == 0U) {
            break;
        }

        if (win_fetch_append(&body, &blen, &bcap, buf, (size_t)rd) != 0) {
            goto cleanup;
        }

        transferred += (unsigned long long)rd;
        if (on_progress != NULL) {
            on_progress(transferred, total_hint, progress_user);
        }
    }

    *out_body = body;
    *out_len = blen;
    body = NULL;
    rc = 0;

cleanup:
    free(body);
    if (hRequest != NULL) {
        (void)WinHttpCloseHandle(hRequest);
    }
    if (hConnect != NULL) {
        (void)WinHttpCloseHandle(hConnect);
    }
    if (hSession != NULL) {
        (void)WinHttpCloseHandle(hSession);
    }
    free(whost);
    free(wreqpath);
    return rc;
}
