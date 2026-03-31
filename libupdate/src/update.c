#include "update.h"

#include <string.h>

static int s_initialized;
static update_options_t s_opts;

static int opts_valid(const update_options_t *opts)
{
    if (opts == NULL) {
        return 0;
    }
    if (opts->update_url == NULL || opts->update_url[0] == '\0') {
        return 0;
    }
    if (opts->app_name == NULL || opts->app_name[0] == '\0') {
        return 0;
    }
    return 1;
}

int update_init(const update_options_t *opts)
{
    if (!opts_valid(opts)) {
        return UPDATE_ERROR;
    }

    s_opts = *opts;
    s_initialized = 1;
    return UPDATE_OK;
}

int update_check(update_info_t *out)
{
    if (out == NULL) {
        return UPDATE_ERROR;
    }
    if (!s_initialized) {
        return UPDATE_ERROR;
    }

    memset(out, 0, sizeof(*out));
    return UPDATE_NOT_AVAILABLE;
}

int update_download(const char *dest_path)
{
    if (!s_initialized) {
        return UPDATE_ERROR;
    }
    if (dest_path == NULL || dest_path[0] == '\0') {
        return UPDATE_ERROR;
    }

    return UPDATE_ERROR;
}

int update_apply(const char *package_path)
{
    if (!s_initialized) {
        return UPDATE_ERROR;
    }
    if (package_path == NULL || package_path[0] == '\0') {
        return UPDATE_ERROR;
    }

    return UPDATE_ERROR;
}

int update_perform(void)
{
    update_info_t info;
    int status;

    status = update_check(&info);
    if (status == UPDATE_ERROR) {
        return UPDATE_ERROR;
    }
    if (status == UPDATE_NOT_AVAILABLE) {
        return UPDATE_OK;
    }
    if (status == UPDATE_AVAILABLE) {
        return UPDATE_ERROR;
    }

    return UPDATE_ERROR;
}
