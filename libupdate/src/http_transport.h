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

#endif /* HTTP_TRANSPORT_H */
