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
 * All functions return int status codes (see update_status_t). Additional
 * values may be added in the future; treat unknown positive codes as UPDATE_ERROR.
 *
 * Required: update_url and app_name non-NULL and non-empty.
 * Call update_init before other entry points.
 */
UPDATE_API int update_init(const update_options_t *opts);
UPDATE_API int update_check(update_info_t *out);
UPDATE_API int update_download(const char *dest_path);
UPDATE_API int update_apply(const char *package_path);
UPDATE_API int update_perform(void);

#ifdef __cplusplus
}
#endif

#endif /* UPDATE_H */
