#include "update.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__CYGWIN__)
    #include <windows.h>
static SRWLOCK s_ctx_lock = SRWLOCK_INIT;

static void ctx_lock(void)
{
    AcquireSRWLockExclusive(&s_ctx_lock);
}

static void ctx_unlock(void)
{
    ReleaseSRWLockExclusive(&s_ctx_lock);
}
#else
    #include <pthread.h>
static pthread_mutex_t s_ctx_lock = PTHREAD_MUTEX_INITIALIZER;

static void ctx_lock(void)
{
    (void)pthread_mutex_lock(&s_ctx_lock);
}

static void ctx_unlock(void)
{
    (void)pthread_mutex_unlock(&s_ctx_lock);
}
#endif

typedef struct {
    int initialized;
    char *update_url;
    char *app_name;
    char *install_dir;
    char *temp_dir;
    char *channel;
} update_context_t;

static update_context_t s_ctx;

static char *dup_str(const char *s)
{
    size_t len;
    char *copy;

    if (s == NULL) {
        return NULL;
    }

    len = strlen(s);
    copy = (char *)malloc(len + 1U);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, s, len + 1U);
    return copy;
}

static void context_free_strings(update_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    free(ctx->update_url);
    free(ctx->app_name);
    free(ctx->install_dir);
    free(ctx->temp_dir);
    free(ctx->channel);
    ctx->update_url = NULL;
    ctx->app_name = NULL;
    ctx->install_dir = NULL;
    ctx->temp_dir = NULL;
    ctx->channel = NULL;
    ctx->initialized = 0;
}

static int opts_valid_locked(const update_options_t *opts)
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

static update_context_t *get_context(void)
{
    return s_ctx.initialized ? &s_ctx : NULL;
}

static int is_initialized(void)
{
    return s_ctx.initialized ? 1 : 0;
}

int update_init(const update_options_t *opts)
{
    ctx_lock();

    if (is_initialized()) {
        ctx_unlock();
        return UPDATE_ERROR;
    }

    if (!opts_valid_locked(opts)) {
        ctx_unlock();
        return UPDATE_ERROR;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));

    s_ctx.update_url = dup_str(opts->update_url);
    s_ctx.app_name = dup_str(opts->app_name);

    if (s_ctx.update_url == NULL || s_ctx.app_name == NULL) {
        context_free_strings(&s_ctx);
        ctx_unlock();
        return UPDATE_ERROR;
    }

    if (opts->install_dir != NULL) {
        s_ctx.install_dir = dup_str(opts->install_dir);
        if (s_ctx.install_dir == NULL) {
            context_free_strings(&s_ctx);
            ctx_unlock();
            return UPDATE_ERROR;
        }
    }

    if (opts->temp_dir != NULL) {
        s_ctx.temp_dir = dup_str(opts->temp_dir);
        if (s_ctx.temp_dir == NULL) {
            context_free_strings(&s_ctx);
            ctx_unlock();
            return UPDATE_ERROR;
        }
    }

    if (opts->channel != NULL) {
        s_ctx.channel = dup_str(opts->channel);
        if (s_ctx.channel == NULL) {
            context_free_strings(&s_ctx);
            ctx_unlock();
            return UPDATE_ERROR;
        }
    }

    s_ctx.initialized = 1;

    ctx_unlock();
    return UPDATE_OK;
}

int update_check(update_info_t *out)
{
    int result;

    ctx_lock();

    if (get_context() == NULL) {
        ctx_unlock();
        return UPDATE_ERROR;
    }

    if (out == NULL) {
        ctx_unlock();
        return UPDATE_ERROR;
    }

    memset(out, 0, sizeof(*out));
    result = UPDATE_NOT_AVAILABLE;

    ctx_unlock();
    return result;
}

int update_download(const char *dest_path)
{
    ctx_lock();

    if (get_context() == NULL) {
        ctx_unlock();
        return UPDATE_ERROR;
    }

    if (dest_path == NULL || dest_path[0] == '\0') {
        ctx_unlock();
        return UPDATE_ERROR;
    }

    ctx_unlock();
    return UPDATE_ERROR;
}

int update_apply(const char *package_path)
{
    ctx_lock();

    if (get_context() == NULL) {
        ctx_unlock();
        return UPDATE_ERROR;
    }

    if (package_path == NULL || package_path[0] == '\0') {
        ctx_unlock();
        return UPDATE_ERROR;
    }

    ctx_unlock();
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
