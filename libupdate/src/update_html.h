#ifndef UPDATE_HTML_H
#define UPDATE_HTML_H

#include <stddef.h>

#define UPDATE_HTML_DESC_MAX_IN (512u * 1024u)
#define UPDATE_HTML_DESC_MAX_OUT ((1024u * 1024u) + 128u)

int update_html_sanitize_alloc(const char *in, size_t in_len, char **out, size_t max_out_total);
int update_html_escape_plain_alloc(const char *in, size_t in_len, char **out, size_t max_out_total);

#endif /* UPDATE_HTML_H */
