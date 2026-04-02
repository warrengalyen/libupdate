#ifndef UPDATE_H
#define UPDATE_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef UPDATE_EXPORTS
        #define UPDATE_API __declspec(dllexport)
    #else
        #define UPDATE_API __declspec(dllimport)
    #endif
#else
    #if defined(__GNUC__) && __GNUC__ >= 4
        #define UPDATE_API __attribute__((visibility("default")))
    #else
        #define UPDATE_API
    #endif
#endif

/**
 * Public ABI surface is plain C function exports and the structs below.
 * Do not rely on undocumented layout beyond the documented field sizes.
 *
 * update_options_t stores pointers only; strings must remain valid until the
 * next successful update_init() replaces the configuration.
 */

typedef struct {
    const char *update_url;
    const char *app_name;
    const char *install_dir; /* optional: may be NULL */
    const char *temp_dir;    /* optional: may be NULL */
    const char *channel;     /* optional: may be NULL */
    /**
     * Optional: 64 hexadecimal SHA-256 digest (ASCII). If set, update_download
     * verifies the file at dest_path after a successful transfer.
     */
    const char *expected_sha256;
} update_options_t;

typedef struct {
    char version[32];
    char download_url[512];
    char checksum[65];
} update_info_t;

typedef enum {
    UPDATE_OK = 0,
    UPDATE_AVAILABLE = 1,
    UPDATE_NOT_AVAILABLE = 2,
    UPDATE_ERROR = 3
} update_status_t;

/**
 * Optional download progress hook. bytes_total_hint is 0 when the size is unknown.
 */
typedef void (*update_download_progress_fn)(unsigned long long bytes_done,
    unsigned long long bytes_total_hint,
    void *user);

UPDATE_API void update_set_download_progress_callback(update_download_progress_fn cb, void *user);

/**
 * Verifies file against a 64-character hex SHA-256 digest (optional leading /
 * trailing ASCII whitespace is ignored).
 */
UPDATE_API int update_verify(const char *file, const char *expected_hash);

/**
 * Extracts a ZIP archive to dest_dir. Refuses encrypted entries and any path
 * that would escape dest_dir (zip slip).
 */
UPDATE_API int update_extract(const char *zip_path, const char *dest_dir);

/**
 * Waits until the process identified by parent_pid is no longer running.
 * Windows: OpenProcess + WaitForSingleObject. POSIX: poll kill(pid, 0) until ESRCH.
 */
UPDATE_API int update_wait_for_parent_exit(int parent_pid);

/** Recursively copies src_dir into dst_dir with retries on transient failures. */
UPDATE_API int update_copy_tree(const char *src_dir, const char *dst_dir);

/** Recursively removes a file or directory tree with retries. */
UPDATE_API int update_remove_tree(const char *path);

/**
 * Renames/moves src to dst with retries (same-directory renames are atomic on
 * local filesystems: POSIX rename(2); Windows MoveFileEx).
 */
UPDATE_API int update_move_path(const char *src, const char *dst);

/** Starts executable_path detached (argv is [path, NULL]). */
UPDATE_API int update_relaunch_app(const char *executable_path);

/**
 * All functions return int status codes (see update_status_t). Additional
 * values may be added in the future; treat unknown positive codes as UPDATE_ERROR.
 *
 * Required: update_url and app_name non-NULL and non-empty.
 * Call update_init before other entry points.
 */
UPDATE_API int update_init(const update_options_t *opts);

/**
 * GETs update_url and expects a small JSON object with string fields:
 *   "version", "download_url", "checksum"
 * Compares "version" to the built-in baseline (override when compiling this
 * library with -DUPDATE_APP_VERSION_STRING=\"1.0.0\"). Returns UPDATE_AVAILABLE
 * when the remote version is strictly newer, UPDATE_NOT_AVAILABLE otherwise,
 * or UPDATE_ERROR on transport/parse failures.
 */
UPDATE_API int update_check(update_info_t *out);
UPDATE_API int update_download(const char *dest_path);

/**
 * Spawns `<exe_dir>/updater(.exe)` with:
 *   --install <package_path> --target <install_dir> --pid <current_pid> --app <exe_path>
 * install_dir is options.install_dir when set, otherwise the directory of the current executable.
 * Windows: arguments are joined with MSVC-compatible quoting for CreateProcessA.
 * POSIX: argv is passed to execv (no shell, no extra escaping).
 * On success, flushes stdio and terminates the process with exit(0) (does not return).
 */
UPDATE_API int update_apply(const char *package_path);
UPDATE_API int update_perform(void);

#ifdef __cplusplus
}
#endif

#endif  /* UPDATE_H */
