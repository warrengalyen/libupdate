#ifndef JSON_MINI_H
#define JSON_MINI_H

#include <stddef.h>

const char *json_mini_skip_ws(const char *p);

/**
 * Extract a JSON string value by key from a flat JSON object.
 * Returns 0 on success, -1 on missing key or parse error.
 */
int json_mini_extract_string(const char *json, const char *key, char *out, size_t outcap);

#endif /* JSON_MINI_H */
