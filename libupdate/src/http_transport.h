#ifndef HTTP_TRANSPORT_H
#define HTTP_TRANSPORT_H

#include <stddef.h>

/**
 * total_hint is 0 when the response size is unknown (no Content-Length).
 * Invoked from the HTTP worker thread / call stack (same thread as download).
 */
typedef void (*update_http_progress_fn)(unsigned long long transferred,
    unsigned long long total_hint,
    void *user);

/**
 * Streams url to dest_path using a temporary file next to dest, then renames.
 * Returns 0 on success, non-zero on failure (caller maps to UPDATE_ERROR).
 */
int update_http_stream_download(const char *url,
    const char *dest_path,
    update_http_progress_fn on_progress,
    void *progress_user);

/**
 * GET url into a growable buffer (NUL-terminated). On success *out_body is malloc'd
 * and must be freed by the caller; *out_len is the body length (excluding the trailing NUL).
 * Responses larger than 512 KiB fail. Returns 0 on success, non-zero on failure.
 */
int update_http_fetch(const char *url,
    char **out_body,
    size_t *out_len,
    update_http_progress_fn on_progress,
    void *progress_user);

#endif /* HTTP_TRANSPORT_H */
