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

/* Public entry points (stubs for now). */
UPDATE_API int update_init(void);
UPDATE_API void update_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* UPDATE_H */
