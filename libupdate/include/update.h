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

/** Configuration for update_init. Strings are copied internally. */
typedef struct {
    const char *update_url;
    const char *app_name;
    const char *install_dir;     /* optional */
    const char *temp_dir;        /* optional */
    const char *channel;         /* optional */
    const char *expected_sha256; /* optional: 64-char hex SHA-256 for download verification */
} update_options_t;

/** Populated by update_check on UPDATE_AVAILABLE. */
typedef struct {
    char version[32];
    char download_url[512];
    char checksum[65];
} update_info_t;

typedef enum {
    UPDATE_OK = 0,
    UPDATE_AVAILABLE = 1,
    UPDATE_NOT_AVAILABLE = 2,
    UPDATE_ERROR = 3,
    UPDATE_NOOP = 4,     /* already up to date */
    UPDATE_STARTED = 5   /* updater spawned; exit the app so it can replace files */
} update_status_t;

/** Progress callback. bytes_total_hint is 0 when unknown. */
typedef void (*update_download_progress_fn)(unsigned long long bytes_done,
    unsigned long long bytes_total_hint,
    void *user);

/** Set download progress callback (requires prior update_init). */
UPDATE_API void update_set_download_progress_callback(update_download_progress_fn cb, void *user);

/** Verify file SHA-256 against a 64-char hex digest. */
UPDATE_API int update_verify(const char *file, const char *expected_hash);

/** Require an absolute path when passed to update_validate_path. */
#define UPDATE_PATH_REQUIRE_ABSOLUTE 1u

/** Reject control chars, ".." segments, reserved names, and over-long paths. */
UPDATE_API int update_validate_path(const char *path, unsigned flags);

/** Validate that staging/backup/state paths are canonical for install_dir. */
UPDATE_API int update_validate_install_paths(const char *zip_path,
    const char *install_dir,
    const char *staging_dir,
    const char *backup_dir,
    const char *state_path);

/** Optional signature hook. Return UPDATE_OK to allow install, UPDATE_ERROR to abort. */
typedef int (*update_verify_signature_fn)(const char *package_path, void *user);
UPDATE_API void update_set_package_signature_verifier(update_verify_signature_fn fn, void *user);

/** Extract ZIP to dest_dir. Rejects encrypted entries and zip-slip paths. */
UPDATE_API int update_extract(const char *zip_path, const char *dest_dir);

/** Block until parent_pid exits. */
UPDATE_API int update_wait_for_parent_exit(int parent_pid);

/** Recursively copy src_dir into dst_dir with retries. */
UPDATE_API int update_copy_tree(const char *src_dir, const char *dst_dir);

/** Recursively remove a file or directory tree with retries. */
UPDATE_API int update_remove_tree(const char *path);

/** Move/rename src to dst with retries. */
UPDATE_API int update_move_path(const char *src, const char *dst);

/** Launch executable_path as a detached process. */
UPDATE_API int update_relaunch_app(const char *executable_path);

/** Initialize the library. Required before other calls. */
UPDATE_API int update_init(const update_options_t *opts);

/** Release resources and allow re-initialization via update_init. */
UPDATE_API void update_shutdown(void);

/** Fetch remote manifest and compare versions. Populates out on UPDATE_AVAILABLE. */
UPDATE_API int update_check(update_info_t *out);

/** Download url to dest_path. Verifies SHA-256 if expected_sha256 was set in options. */
UPDATE_API int update_download(const char *url, const char *dest_path);

/** Spawn the updater with package_path. Calls exit(0) on success (does not return). */
UPDATE_API int update_apply(const char *package_path);

/** Check, download, verify, and spawn updater. Returns UPDATE_STARTED or UPDATE_NOOP. */
UPDATE_API int update_perform(void);

#ifdef __cplusplus
}
#endif

#endif  /* UPDATE_H */
