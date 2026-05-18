#ifndef JSON_MINI_H
#define JSON_MINI_H

#include <stddef.h>

const char *json_mini_skip_ws(const char *p);

/**
 * Extract a JSON string value by key from a flat JSON object.
 * Returns 0 on success, -1 on missing key or parse error.
 */
int json_mini_extract_string(const char *json, const char *key, char *out, size_t outcap);

/**
 * Optional string: returns 0 on success.
 * *present_out: 1 if key found and parsed, 0 if key absent (out[0]='\0'), -1 malformed (return -1).
 */
int json_mini_extract_string_opt(const char *json, const char *key, char *out, size_t outcap, int *present_out);

/**
 * Allocates extracted string (caller frees). Key absent: *out_alloc=NULL, return 0.
 * max_value_len excludes NUL; longer values or OOM return -1.
 */
int json_mini_extract_string_alloc(const char *json, const char *key, size_t max_value_len, char **out_alloc);

#endif /* JSON_MINI_H */
