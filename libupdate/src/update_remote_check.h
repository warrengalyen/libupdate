#ifndef UPDATE_REMOTE_CHECK_H
#define UPDATE_REMOTE_CHECK_H

#include "http_transport.h"
#include "update.h"

int update_remote_check(const char *url,
    update_http_progress_fn on_progress,
    void *progress_user,
    update_info_t *out);

#endif /* UPDATE_REMOTE_CHECK_H */
