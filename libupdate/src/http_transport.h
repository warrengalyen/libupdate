#ifndef HTTP_TRANSPORT_H
#define HTTP_TRANSPORT_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/** Progress callback. total_hint is 0 when Content-Length is absent. */
typedef void (*update_http_progress_fn)(unsigned long long transferred,
    unsigned long long total_hint,
    void *user);

/** Stream url to dest_path via a temp file, then rename. Returns 0 on success. */
int update_http_stream_download(const char *url,
    const char *dest_path,
    update_http_progress_fn on_progress,
    void *progress_user);

/** GET url into a malloc'd NUL-terminated buffer. Caller frees *out_body. Max 512 KiB. */
int update_http_fetch(const char *url,
    char **out_body,
    size_t *out_len,
    update_http_progress_fn on_progress,
    void *progress_user);

static inline char *http_temp_path_for(const char *dest)
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

#endif /* HTTP_TRANSPORT_H */
