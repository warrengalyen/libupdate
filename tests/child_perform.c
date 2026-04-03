/**
 * Subprocess helper: update_init + update_perform (used so the library waits on this PID, then exits).
 * Environment (all required, absolute paths / full URL):
 *   LIBUPDATE_PERF_URL, LIBUPDATE_PERF_INSTALL, LIBUPDATE_PERF_TEMP, LIBUPDATE_PERF_SHA256
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <update.h>

#include "update_test_access.h"

int main(void)
{
    const char *url = getenv("LIBUPDATE_PERF_URL");
    const char *install = getenv("LIBUPDATE_PERF_INSTALL");
    const char *temp = getenv("LIBUPDATE_PERF_TEMP");
    const char *sha = getenv("LIBUPDATE_PERF_SHA256");
    update_options_t opts;
    int st;

    if (url == NULL || install == NULL || temp == NULL || sha == NULL) {
        return 20;
    }

    memset(&opts, 0, sizeof(opts));
    opts.update_url = url;
    opts.app_name = "libupdate_perform_child";
    opts.install_dir = install;
    opts.temp_dir = temp;
    opts.expected_sha256 = sha;

    update_test_reset_context();
    if (update_init(&opts) != UPDATE_OK) {
        return 21;
    }

    st = update_perform();
    /* Child process must exit after spawning updater so --pid wait completes. */
    if (st == UPDATE_STARTED) {
        return 0;
    }
    if (st == UPDATE_NOOP) {
        return 22;
    }
    return 23;
}
