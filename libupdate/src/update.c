#include "update.h"

#include "http_transport.h"
#include "platform_fs.h"
#include "platform_process.h"
#include "sha256.h"
#include "update_remote_check.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef UPDATE_APPLY_PATH_MAX
    #define UPDATE_APPLY_PATH_MAX 4096
#endif

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
    #include <unistd.h>
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
    char *expected_sha256;
    update_http_progress_fn progress_cb;
    void *progress_userdata;
    update_verify_signature_fn signature_cb;
    void *signature_userdata;
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
    free(ctx->expected_sha256);
    ctx->update_url = NULL;
    ctx->app_name = NULL;
    ctx->install_dir = NULL;
    ctx->temp_dir = NULL;
    ctx->channel = NULL;
    ctx->expected_sha256 = NULL;
    ctx->signature_cb = NULL;
    ctx->signature_userdata = NULL;
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

static int hex_nibble(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static const char *hex_skip_space(const char *s)
{
    while (s != NULL && *s != '\0' && isspace((unsigned char)*s) != 0) {
        s++;
    }
    return s;
}

static size_t hex_trim_end(const char *s, size_t len)
{
    while (len > 0U && isspace((unsigned char)s[len - 1U]) != 0) {
        len--;
    }
    return len;
}

static int parse_sha256_hex(const char *hex, unsigned char out[32])
{
    const char *p;
    size_t L;
    size_t i;

    if (hex == NULL) {
        return -1;
    }

    p = hex_skip_space(hex);
    L = strlen(p);
    L = hex_trim_end(p, L);
    if (L != 64U) {
        return -1;
    }

    for (i = 0U; i < 32U; i++) {
        int hi = hex_nibble((int)p[i * 2U]);
        int lo = hex_nibble((int)p[i * 2U + 1U]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i] = (unsigned char)((hi << 4) | lo);
    }

    return 0;
}

static int sha256_digest_equals_ct(const unsigned char a[32], const unsigned char b[32])
{
    const volatile unsigned char *va = (const volatile unsigned char *)a;
    const volatile unsigned char *vb = (const volatile unsigned char *)b;
    volatile unsigned char d = 0U;
    size_t i;

    for (i = 0U; i < 32U; i++) {
        d = (unsigned char)(d | (unsigned char)(va[i] ^ vb[i]));
    }

    return d == 0U ? 0 : 1;
}

UPDATE_API int update_verify(const char *file, const char *expected_hash)
{
    unsigned char expect_bin[32];
    unsigned char digest[32];
    sha256_ctx ctx;
    FILE *fp;
    unsigned char buf[8192];

    if (file == NULL || file[0] == '\0' || expected_hash == NULL || expected_hash[0] == '\0') {
        return UPDATE_ERROR;
    }

    if (parse_sha256_hex(expected_hash, expect_bin) != 0) {
        return UPDATE_ERROR;
    }

    fp = fopen(file, "rb");
    if (fp == NULL) {
        return UPDATE_ERROR;
    }

    sha256_init(&ctx);
    for (;;) {
        size_t n = fread(buf, 1U, sizeof(buf), fp);
        if (n > 0U) {
            sha256_update(&ctx, buf, n);
        }
        if (n < sizeof(buf)) {
            break;
        }
    }

    if (ferror(fp) != 0) {
        (void)fclose(fp);
        return UPDATE_ERROR;
    }

    (void)fclose(fp);
    sha256_final(&ctx, digest);

    if (sha256_digest_equals_ct(digest, expect_bin) != 0) {
        return UPDATE_ERROR;
    }

    return UPDATE_OK;
}

UPDATE_API int update_init(const update_options_t *opts)
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

    if (opts->expected_sha256 != NULL) {
        s_ctx.expected_sha256 = dup_str(opts->expected_sha256);
        if (s_ctx.expected_sha256 == NULL) {
            context_free_strings(&s_ctx);
            ctx_unlock();
            return UPDATE_ERROR;
        }
    }

    if (s_ctx.install_dir != NULL && s_ctx.install_dir[0] != '\0'
        && update_validate_path(s_ctx.install_dir, UPDATE_PATH_REQUIRE_ABSOLUTE) != UPDATE_OK) {
        context_free_strings(&s_ctx);
        ctx_unlock();
        return UPDATE_ERROR;
    }

    if (s_ctx.temp_dir != NULL && s_ctx.temp_dir[0] != '\0'
        && update_validate_path(s_ctx.temp_dir, UPDATE_PATH_REQUIRE_ABSOLUTE) != UPDATE_OK) {
        context_free_strings(&s_ctx);
        ctx_unlock();
        return UPDATE_ERROR;
    }

    s_ctx.initialized = 1;

    ctx_unlock();
    return UPDATE_OK;
}

UPDATE_API void update_shutdown(void)
{
    ctx_lock();
    context_free_strings(&s_ctx);
    memset(&s_ctx, 0, sizeof(s_ctx));
    ctx_unlock();
}

UPDATE_API void update_set_download_progress_callback(update_download_progress_fn cb, void *user)
{
    ctx_lock();
    if (!is_initialized()) {
        ctx_unlock();
        return;
    }
    s_ctx.progress_cb = (update_http_progress_fn)cb;
    s_ctx.progress_userdata = user;
    ctx_unlock();
}

UPDATE_API void update_set_package_signature_verifier(update_verify_signature_fn fn, void *user)
{
    ctx_lock();
    if (!is_initialized()) {
        ctx_unlock();
        return;
    }
    s_ctx.signature_cb = fn;
    s_ctx.signature_userdata = user;
    ctx_unlock();
}

UPDATE_API int update_check(update_info_t *out)
{
    char *url_copy = NULL;
    update_http_progress_fn prog;
    void *prog_ud;
    int rc;

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
    url_copy = dup_str(s_ctx.update_url);
    prog = s_ctx.progress_cb;
    prog_ud = s_ctx.progress_userdata;

    ctx_unlock();

    if (url_copy == NULL) {
        return UPDATE_ERROR;
    }

    rc = update_remote_check(url_copy, prog, prog_ud, out);
    free(url_copy);
    return rc;
}

UPDATE_API int update_download(const char *url, const char *dest_path)
{
    char *url_copy = NULL;
    char *expect_copy = NULL;
    update_http_progress_fn prog;
    void *prog_ud;
    int http_rc;

    if (url == NULL || url[0] == '\0') {
        return UPDATE_ERROR;
    }

    ctx_lock();

    if (get_context() == NULL) {
        ctx_unlock();
        return UPDATE_ERROR;
    }

    if (dest_path == NULL || dest_path[0] == '\0') {
        ctx_unlock();
        return UPDATE_ERROR;
    }

    if (update_validate_path(dest_path, UPDATE_PATH_REQUIRE_ABSOLUTE) != UPDATE_OK) {
        ctx_unlock();
        return UPDATE_ERROR;
    }

    url_copy = dup_str(url);
    expect_copy = dup_str(s_ctx.expected_sha256);
    prog = s_ctx.progress_cb;
    prog_ud = s_ctx.progress_userdata;

    ctx_unlock();

    if (url_copy == NULL) {
        free(expect_copy);
        return UPDATE_ERROR;
    }

    http_rc = update_http_stream_download(url_copy, dest_path, prog, prog_ud);
    free(url_copy);

    if (http_rc != 0) {
        free(expect_copy);
        return UPDATE_ERROR;
    }

    if (expect_copy != NULL && expect_copy[0] != '\0') {
        if (update_verify(dest_path, expect_copy) != UPDATE_OK) {
            (void)platform_fs_remove_path(dest_path);
            free(expect_copy);
            return UPDATE_ERROR;
        }
    }

    free(expect_copy);
    return UPDATE_OK;
}

static int update_apply_spawn(const char *package_path)
{
    char exe_path[UPDATE_APPLY_PATH_MAX];
    char exe_dir[UPDATE_APPLY_PATH_MAX];
    char updater_path[UPDATE_APPLY_PATH_MAX];
    char install_buf[UPDATE_APPLY_PATH_MAX];
    char pid_buf[32];
    char *pkg_copy = NULL;
    char *install_copy = NULL;
    const char *install_target;
    const char *spawn_argv[10];
    int spawn_rc;
    int child_pid = 0;

    ctx_lock();

    if (get_context() == NULL) {
        ctx_unlock();
        return -1;
    }

    if (package_path == NULL || package_path[0] == '\0') {
        ctx_unlock();
        return -1;
    }

    if (update_validate_path(package_path, 0U) != UPDATE_OK) {
        ctx_unlock();
        return -1;
    }

    pkg_copy = dup_str(package_path);
    if (pkg_copy == NULL) {
        ctx_unlock();
        return -1;
    }

    if (s_ctx.install_dir != NULL && s_ctx.install_dir[0] != '\0') {
        install_copy = dup_str(s_ctx.install_dir);
        if (install_copy == NULL) {
            free(pkg_copy);
            ctx_unlock();
            return -1;
        }
    }

    ctx_unlock();

    if (platform_fs_get_executable_path(exe_path, sizeof exe_path) != PLATFORM_OK) {
        free(pkg_copy);
        free(install_copy);
        return -1;
    }

    if (platform_fs_get_executable_dir(exe_dir, sizeof exe_dir) != PLATFORM_OK) {
        free(pkg_copy);
        free(install_copy);
        return -1;
    }

    if (install_copy != NULL) {
        install_target = install_copy;
    } else {
        if (snprintf(install_buf, sizeof install_buf, "%s", exe_dir) >= (int)sizeof install_buf) {
            free(pkg_copy);
            free(install_copy);
            return -1;
        }
        install_target = install_buf;
    }

#if defined(_WIN32) && !defined(__CYGWIN__)
    if (snprintf(updater_path, sizeof updater_path, "%s/updater.exe", exe_dir) >= (int)sizeof updater_path) {
        free(pkg_copy);
        free(install_copy);
        return -1;
    }
#else
    if (snprintf(updater_path, sizeof updater_path, "%s/updater", exe_dir) >= (int)sizeof updater_path) {
        free(pkg_copy);
        free(install_copy);
        return -1;
    }
#endif

    if (update_validate_path(install_target, UPDATE_PATH_REQUIRE_ABSOLUTE) != UPDATE_OK
        || update_validate_path(exe_path, UPDATE_PATH_REQUIRE_ABSOLUTE) != UPDATE_OK
        || update_validate_path(updater_path, UPDATE_PATH_REQUIRE_ABSOLUTE) != UPDATE_OK) {
        free(pkg_copy);
        free(install_copy);
        return -1;
    }

    if (snprintf(pid_buf, sizeof pid_buf, "%d", platform_process_get_current_pid()) >= (int)sizeof pid_buf) {
        free(pkg_copy);
        free(install_copy);
        return -1;
    }

    spawn_argv[0] = updater_path;
    spawn_argv[1] = "--install";
    spawn_argv[2] = pkg_copy;
    spawn_argv[3] = "--target";
    spawn_argv[4] = install_target;
    spawn_argv[5] = "--pid";
    spawn_argv[6] = pid_buf;
    spawn_argv[7] = "--app";
    spawn_argv[8] = exe_path;
    spawn_argv[9] = NULL;

    spawn_rc = platform_process_spawn(updater_path, spawn_argv, &child_pid);
    free(pkg_copy);
    free(install_copy);

    if (spawn_rc != PLATFORM_OK) {
        return -1;
    }

    (void)child_pid;
    return 0;
}

UPDATE_API int update_apply(const char *package_path)
{
    if (update_apply_spawn(package_path) != 0) {
        return UPDATE_ERROR;
    }

    (void)fflush(NULL);
    exit(0);
}

UPDATE_API int update_perform(void)
{
    update_info_t info;
    int st;
    char temp_base[UPDATE_APPLY_PATH_MAX];
    char workdir[UPDATE_APPLY_PATH_MAX];
    char zip_path[UPDATE_APPLY_PATH_MAX + 32];
    char *temp_override = NULL;
    char *dl_url = NULL;
    char *checksum_copy = NULL;
    update_http_progress_fn prog;
    void *prog_ud;
    unsigned attempt;
    int http_rc;

    st = update_check(&info);
    if (st == UPDATE_ERROR) {
        return UPDATE_ERROR;
    }
    if (st == UPDATE_NOT_AVAILABLE) {
        return UPDATE_NOOP;
    }
    if (st != UPDATE_AVAILABLE) {
        return UPDATE_ERROR;
    }

    ctx_lock();
    if (get_context() == NULL) {
        ctx_unlock();
        return UPDATE_ERROR;
    }
    if (s_ctx.temp_dir != NULL && s_ctx.temp_dir[0] != '\0') {
        temp_override = dup_str(s_ctx.temp_dir);
        if (temp_override == NULL) {
            ctx_unlock();
            return UPDATE_ERROR;
        }
    }
    prog = s_ctx.progress_cb;
    prog_ud = s_ctx.progress_userdata;
    ctx_unlock();

    if (temp_override != NULL) {
        if (snprintf(temp_base, sizeof temp_base, "%s", temp_override) >= (int)sizeof temp_base) {
            free(temp_override);
            return UPDATE_ERROR;
        }
        free(temp_override);
        temp_override = NULL;
    } else {
        if (platform_fs_get_system_temp_dir(temp_base, sizeof temp_base) != PLATFORM_OK) {
            return UPDATE_ERROR;
        }
    }

    for (attempt = 0U; attempt < 8U; attempt++) {
        unsigned long long tag;

#if defined(_WIN32) && !defined(__CYGWIN__)
        tag = (unsigned long long)(unsigned)GetTickCount();
        tag ^= (unsigned long long)(unsigned)platform_process_get_current_pid() << 20;
        tag += (unsigned long long)attempt;
#else
        tag = (unsigned long long)time(NULL);
        tag ^= (unsigned long long)(unsigned)getpid() << 24;
        tag += (unsigned long long)attempt;
#endif

        if (snprintf(workdir, sizeof workdir, "%s/libupdate_%llx", temp_base, (unsigned long long)tag)
            >= (int)sizeof workdir) {
            return UPDATE_ERROR;
        }

        if (platform_fs_create_directory_recursive(workdir) == PLATFORM_OK) {
            break;
        }
    }

    if (attempt >= 8U) {
        return UPDATE_ERROR;
    }

    if (snprintf(zip_path, sizeof zip_path, "%s/package.bin", workdir) >= (int)sizeof zip_path) {
        (void)update_remove_tree(workdir);
        return UPDATE_ERROR;
    }

    if (update_validate_path(temp_base, UPDATE_PATH_REQUIRE_ABSOLUTE) != UPDATE_OK
        || update_validate_path(workdir, UPDATE_PATH_REQUIRE_ABSOLUTE) != UPDATE_OK
        || update_validate_path(zip_path, UPDATE_PATH_REQUIRE_ABSOLUTE) != UPDATE_OK) {
        (void)update_remove_tree(workdir);
        return UPDATE_ERROR;
    }

    dl_url = dup_str(info.download_url);
    checksum_copy = dup_str(info.checksum);
    if (dl_url == NULL || checksum_copy == NULL) {
        free(dl_url);
        free(checksum_copy);
        (void)update_remove_tree(workdir);
        return UPDATE_ERROR;
    }

    http_rc = update_http_stream_download(dl_url, zip_path, prog, prog_ud);
    free(dl_url);
    dl_url = NULL;

    if (http_rc != 0) {
        free(checksum_copy);
        (void)update_remove_tree(workdir);
        return UPDATE_ERROR;
    }

    if (update_verify(zip_path, checksum_copy) != UPDATE_OK) {
        free(checksum_copy);
        (void)platform_fs_remove_path(zip_path);
        (void)update_remove_tree(workdir);
        return UPDATE_ERROR;
    }

    free(checksum_copy);
    checksum_copy = NULL;

    {
        update_verify_signature_fn sig;
        void *sig_ud;

        ctx_lock();
        sig = s_ctx.signature_cb;
        sig_ud = s_ctx.signature_userdata;
        ctx_unlock();

        if (sig != NULL && sig(zip_path, sig_ud) != UPDATE_OK) {
            (void)platform_fs_remove_path(zip_path);
            (void)update_remove_tree(workdir);
            return UPDATE_ERROR;
        }
    }

    if (update_apply_spawn(zip_path) != 0) {
        (void)update_remove_tree(workdir);
        return UPDATE_ERROR;
    }

    return UPDATE_STARTED;
}

#if defined(UPDATE_BUILD_TEST_HOOKS)
UPDATE_API const char *update_test_get_install_dir(void)
{
    const char *p;

    ctx_lock();
    p = s_ctx.install_dir;
    ctx_unlock();
    return p;
}

UPDATE_API const char *update_test_get_update_url(void)
{
    const char *p;

    ctx_lock();
    p = s_ctx.update_url;
    ctx_unlock();
    return p;
}

UPDATE_API void update_test_reset_context(void)
{
    ctx_lock();
    context_free_strings(&s_ctx);
    memset(&s_ctx, 0, sizeof(s_ctx));
    ctx_unlock();
}
#endif
