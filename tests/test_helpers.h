#ifndef LIBUPDATE_TEST_HELPERS_H
#define LIBUPDATE_TEST_HELPERS_H

#include <stddef.h>

/** Unique temp directory for the current test (absolute path, no trailing slash). */
const char *test_env_root(void);

/** Create a fresh per-test tree; safe to call from setUp. */
void test_env_setup(void);

/** Remove per-test tree; safe to call from tearDown. */
void test_env_cleanup(void);

/** Join test_env_root() with a relative path using '/' (works on Windows for APIs we use). */
int test_env_join(char *out, size_t cap, const char *rel);

/** Path to sibling executable in the same directory as the running test binary. */
int test_bin_dir_join(char *out, size_t cap, const char *name);

/** Start mock HTTP server (Python). Returns 0 and writes pid on success. */
int test_http_server_start(const char *serve_root, int port, int *out_pid);

/** Stop server started with test_http_server_start. */
void test_http_server_stop(int pid);

/** Write update.json with placeholders replaced. */
int test_write_update_manifest(const char *serve_root, int port, const char *sha256_hex);

#endif
