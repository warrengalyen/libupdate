/*
 * HTTP/1.1 transport (POSIX sockets) with optional OpenSSL TLS.
 *
 * Limitations:
 *   - HTTP redirects (301/302/307) are not followed.
 */

#include "http_transport.h"
#include "platform_fs.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef LIBUPDATE_HAVE_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

/* ── connection abstraction ─────────────────────────────────────── */

typedef struct {
    int fd;
#ifdef LIBUPDATE_HAVE_OPENSSL
    SSL_CTX *ssl_ctx;
    SSL *ssl;
#endif
} conn_t;

static void conn_init(conn_t *c)
{
    c->fd = -1;
#ifdef LIBUPDATE_HAVE_OPENSSL
    c->ssl_ctx = NULL;
    c->ssl = NULL;
#endif
}

static int conn_connect(conn_t *c, struct addrinfo *ai, const char *host, int use_tls)
{
    c->fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (c->fd < 0) {
        return -1;
    }

    if (connect(c->fd, ai->ai_addr, ai->ai_addrlen) != 0) {
        return -1;
    }

    if (!use_tls) {
        return 0;
    }

#ifdef LIBUPDATE_HAVE_OPENSSL
    c->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (c->ssl_ctx == NULL) {
        return -1;
    }

    SSL_CTX_set_default_verify_paths(c->ssl_ctx);
    SSL_CTX_set_verify(c->ssl_ctx, SSL_VERIFY_PEER, NULL);

    c->ssl = SSL_new(c->ssl_ctx);
    if (c->ssl == NULL) {
        return -1;
    }

    SSL_set_tlsext_host_name(c->ssl, host);
    SSL_set_fd(c->ssl, c->fd);

    if (SSL_connect(c->ssl) != 1) {
        return -1;
    }

    return 0;
#else
    (void)host;
    return -1;
#endif
}

static int conn_send(conn_t *c, const void *buf, size_t len)
{
#ifdef LIBUPDATE_HAVE_OPENSSL
    if (c->ssl != NULL) {
        int w = SSL_write(c->ssl, buf, (int)len);
        return w == (int)len ? 0 : -1;
    }
#endif
    {
        ssize_t w = send(c->fd, buf, len, 0);
        return w == (ssize_t)len ? 0 : -1;
    }
}

static int conn_recv(conn_t *c, void *buf, size_t cap)
{
#ifdef LIBUPDATE_HAVE_OPENSSL
    if (c->ssl != NULL) {
        int r = SSL_read(c->ssl, buf, (int)cap);
        if (r < 0) {
            return -1;
        }
        return r;
    }
#endif
    {
        ssize_t r = recv(c->fd, buf, cap, 0);
        if (r < 0) {
            return -1;
        }
        return (int)r;
    }
}

static void conn_close(conn_t *c)
{
#ifdef LIBUPDATE_HAVE_OPENSSL
    if (c->ssl != NULL) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
        c->ssl = NULL;
    }
    if (c->ssl_ctx != NULL) {
        SSL_CTX_free(c->ssl_ctx);
        c->ssl_ctx = NULL;
    }
#endif
    if (c->fd >= 0) {
        (void)close(c->fd);
        c->fd = -1;
    }
}

/* ── buffered reader over conn_t ────────────────────────────────── */

typedef struct {
    const unsigned char *pfx;
    size_t pfx_len;
    size_t pfx_pos;
    conn_t *conn;
} io_src;

static int src_read(io_src *s, void *buf, size_t cap)
{
    unsigned char *out = (unsigned char *)buf;
    size_t got = 0U;

    while (got < cap) {
        if (s->pfx_pos < s->pfx_len) {
            size_t n = s->pfx_len - s->pfx_pos;
            if (n > cap - got) {
                n = cap - got;
            }
            memcpy(out + got, s->pfx + s->pfx_pos, n);
            s->pfx_pos += n;
            got += n;
            continue;
        }

        {
            int rd = conn_recv(s->conn, out + got, cap - got);
            if (rd < 0) {
                return -1;
            }
            if (rd == 0) {
                return (int)got;
            }
            got += (size_t)rd;
        }
    }

    return (int)got;
}

/* ── helpers ─────────────────────────────────────────────────────── */

static int append_grow(char **buf, size_t *len, size_t *cap, const void *chunk, size_t chunk_len)
{
    size_t need;

    need = *len + chunk_len + 1U;
    if (need > *cap) {
        size_t ncap = *cap == 0U ? 4096U : *cap * 2U;
        while (ncap < need) {
            ncap *= 2U;
        }
        {
            char *nb = (char *)realloc(*buf, ncap);
            if (nb == NULL) {
                return -1;
            }
            *buf = nb;
            *cap = ncap;
        }
    }

    memcpy(*buf + *len, chunk, chunk_len);
    *len += chunk_len;
    (*buf)[*len] = '\0';
    return 0;
}

static int read_http_headers(conn_t *c, char **out_headers, size_t *out_total_len)
{
    char *buf = NULL;
    size_t len = 0U;
    size_t cap = 0U;

    for (;;) {
        char tmp[2048];
        int rd = conn_recv(c, tmp, sizeof(tmp));
        if (rd < 0) {
            free(buf);
            return -1;
        }
        if (rd == 0) {
            free(buf);
            return -1;
        }
        if (append_grow(&buf, &len, &cap, tmp, (size_t)rd) != 0) {
            free(buf);
            return -1;
        }
        if (len > 256U * 1024U) {
            free(buf);
            return -1;
        }
        if (strstr(buf, "\r\n\r\n") != NULL) {
            break;
        }
    }

    *out_headers = buf;
    *out_total_len = len;
    return 0;
}

static int parse_http_url(const char *url,
    char *host,
    size_t hostcap,
    char *path,
    size_t pathcap,
    int *out_tls,
    unsigned *port_out)
{
    const char *p;
    const char *pathstart;
    const char *host_end;
    const char *colon;
    size_t hostlen;

    if (strncmp(url, "https://", 8) == 0) {
        *out_tls = 1;
        *port_out = 443U;
        p = url + 8U;
    } else if (strncmp(url, "http://", 7) == 0) {
        *out_tls = 0;
        *port_out = 80U;
        p = url + 7U;
    } else {
        return -1;
    }

    pathstart = strchr(p, '/');
    if (pathstart == NULL) {
        host_end = p + strlen(p);
        if ((size_t)(host_end - p) + 1U > hostcap) {
            return -1;
        }
        memcpy(host, p, (size_t)(host_end - p));
        host[(size_t)(host_end - p)] = '\0';
        if (pathcap < 2U) {
            return -1;
        }
        path[0] = '/';
        path[1] = '\0';
        return 0;
    }

    host_end = pathstart;
    colon = memchr(p, ':', (size_t)(host_end - p));
    if (colon != NULL) {
        hostlen = (size_t)(colon - p);
        if (hostlen + 1U > hostcap) {
            return -1;
        }
        memcpy(host, p, hostlen);
        host[hostlen] = '\0';
        *port_out = (unsigned)strtoul(colon + 1U, NULL, 10);
        if (*port_out == 0U) {
            *port_out = *out_tls ? 443U : 80U;
        }
    } else {
        hostlen = (size_t)(host_end - p);
        if (hostlen + 1U > hostcap) {
            return -1;
        }
        memcpy(host, p, hostlen);
        host[hostlen] = '\0';
    }

    {
        size_t plen = strlen(pathstart);
        if (plen + 1U > pathcap) {
            return -1;
        }
        memcpy(path, pathstart, plen + 1U);
    }

    return 0;
}

static int parse_status_200(const char *headers)
{
    const char *line_end = strstr(headers, "\r\n");
    const char *p;
    int code = 0;

    if (line_end == NULL) {
        return -1;
    }

    p = headers;
    while (p < line_end && !isdigit((unsigned char)*p)) {
        p++;
    }
    if (p >= line_end) {
        return -1;
    }

    code = atoi(p);
    return code == 200 ? 0 : -1;
}

static int header_value_line(const char *headers, const char *name, char *out, size_t outcap)
{
    const char *p = headers;
    size_t nlen = strlen(name);

    while (p != NULL && *p != '\0') {
        const char *line_end = strstr(p, "\r\n");
        const char *colon;

        if (line_end == NULL) {
            break;
        }

        if ((size_t)(line_end - p) >= nlen && strncasecmp(p, name, nlen) == 0) {
            colon = memchr(p, ':', (size_t)(line_end - p));
            if (colon == NULL) {
                break;
            }
            colon++;
            while (colon < line_end && (*colon == ' ' || *colon == '\t')) {
                colon++;
            }
            {
                size_t vlen = (size_t)(line_end - colon);
                if (vlen + 1U > outcap) {
                    return -1;
                }
                memcpy(out, colon, vlen);
                out[vlen] = '\0';
                return 0;
            }
        }

        p = line_end + 2U;
    }

    return -1;
}

static int contains_token_ci(const char *haystack, const char *token)
{
    size_t ti;
    size_t hi;

    for (hi = 0; haystack[hi] != '\0'; hi++) {
        for (ti = 0; token[ti] != '\0'; ti++) {
            unsigned char a = (unsigned char)haystack[hi + ti];
            unsigned char b = (unsigned char)token[ti];
            if (tolower(a) != tolower(b)) {
                break;
            }
        }
        if (token[ti] == '\0') {
            return 1;
        }
    }

    return 0;
}

static int is_chunked_encoding(const char *headers)
{
    char val[128];
    if (header_value_line(headers, "Transfer-Encoding:", val, sizeof(val)) != 0) {
        return 0;
    }
    return contains_token_ci(val, "chunked");
}

static int parse_content_length(const char *headers, unsigned long long *out_len, int *found)
{
    char val[64];
    if (header_value_line(headers, "Content-Length:", val, sizeof(val)) != 0) {
        *found = 0;
        *out_len = 0ULL;
        return 0;
    }
    *found = 1;
    *out_len = strtoull(val, NULL, 10);
    return 0;
}

/* ── body readers (file destination) ────────────────────────────── */

static int read_line_src(io_src *s, char *line, size_t cap)
{
    size_t pos = 0U;

    while (pos + 1U < cap) {
        char c;
        int rd = src_read(s, &c, 1U);
        if (rd != 1) {
            return -1;
        }
        line[pos++] = c;
        if (pos >= 2U && line[pos - 1] == '\n' && line[pos - 2] == '\r') {
            line[pos] = '\0';
            return 0;
        }
    }

    return -1;
}

static int read_body_known_src(io_src *s,
    FILE *out,
    unsigned long long nbytes,
    update_http_progress_fn on_progress,
    void *progress_user,
    unsigned long long total_hint)
{
    unsigned char buf[8192];
    unsigned long long done = 0ULL;

    while (done < nbytes) {
        unsigned long long want = nbytes - done;
        size_t chunk = want > sizeof(buf) ? sizeof(buf) : (size_t)want;
        int rd = src_read(s, buf, chunk);
        if (rd <= 0) {
            return -1;
        }
        if (fwrite(buf, 1U, (size_t)rd, out) != (size_t)rd) {
            return -1;
        }
        done += (unsigned long long)(size_t)rd;
        if (on_progress != NULL) {
            on_progress(done, total_hint, progress_user);
        }
    }

    return 0;
}

static int read_body_chunked_src(io_src *s,
    FILE *out,
    update_http_progress_fn on_progress,
    void *progress_user)
{
    unsigned char buf[8192];
    unsigned long long done = 0ULL;

    for (;;) {
        char line[96];
        unsigned long long chunk_sz;
        unsigned long long i;

        if (read_line_src(s, line, sizeof(line)) != 0) {
            return -1;
        }
        chunk_sz = strtoull(line, NULL, 16);
        if (chunk_sz == 0ULL) {
            if (read_line_src(s, line, sizeof(line)) != 0) {
                return -1;
            }
            break;
        }

        for (i = 0ULL; i < chunk_sz;) {
            size_t want = (size_t)(chunk_sz - i);
            if (want > sizeof(buf)) {
                want = sizeof(buf);
            }
            {
                int rd = src_read(s, buf, want);
                if (rd <= 0) {
                    return -1;
                }
                if (fwrite(buf, 1U, (size_t)rd, out) != (size_t)rd) {
                    return -1;
                }
                i += (unsigned long long)(size_t)rd;
                done += (unsigned long long)(size_t)rd;
                if (on_progress != NULL) {
                    on_progress(done, 0ULL, progress_user);
                }
            }
        }

        if (read_line_src(s, line, sizeof(line)) != 0) {
            return -1;
        }
    }

    return 0;
}

static int read_body_until_close_src(io_src *s,
    FILE *out,
    update_http_progress_fn on_progress,
    void *progress_user,
    unsigned long long total_hint)
{
    unsigned char buf[8192];
    unsigned long long done = 0ULL;

    for (;;) {
        int rd = src_read(s, buf, sizeof(buf));
        if (rd < 0) {
            return -1;
        }
        if (rd == 0) {
            break;
        }
        if (fwrite(buf, 1U, (size_t)rd, out) != (size_t)rd) {
            return -1;
        }
        done += (unsigned long long)(size_t)rd;
        if (on_progress != NULL) {
            on_progress(done, total_hint, progress_user);
        }
    }

    return 0;
}

/* ── stream download ────────────────────────────────────────────── */

int update_http_stream_download(const char *url,
    const char *dest_path,
    update_http_progress_fn on_progress,
    void *progress_user)
{
    char host[512];
    char path[4096];
    unsigned port = 80U;
    int use_tls = 0;
    char portbuf[16];
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    conn_t conn;
    char req[8192];
    int req_len;
    char *headers = NULL;
    size_t header_total = 0U;
    const char *body_sep;
    size_t body_prefix;
    int cl_found = 0;
    unsigned long long content_len = 0ULL;
    int chunked = 0;
    io_src src;
    FILE *out = NULL;
    char *tmp_path = NULL;
    int rc = -1;

    if (url == NULL || dest_path == NULL || dest_path[0] == '\0') {
        return -1;
    }

    conn_init(&conn);

    tmp_path = http_temp_path_for(dest_path);
    if (tmp_path == NULL) {
        return -1;
    }

    if (parse_http_url(url, host, sizeof(host), path, sizeof(path), &use_tls, &port) != 0) {
        goto cleanup;
    }

    (void)snprintf(portbuf, sizeof(portbuf), "%u", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, portbuf, &hints, &res) != 0) {
        goto cleanup;
    }

    if (conn_connect(&conn, res, host, use_tls) != 0) {
        goto cleanup;
    }

    req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "User-Agent: libupdate/1.0\r\n"
        "Accept: */*\r\n"
        "\r\n",
        path,
        host);
    if (req_len <= 0 || (size_t)req_len >= sizeof(req)) {
        goto cleanup;
    }

    if (conn_send(&conn, req, (size_t)req_len) != 0) {
        goto cleanup;
    }

    if (read_http_headers(&conn, &headers, &header_total) != 0) {
        goto cleanup;
    }

    if (parse_status_200(headers) != 0) {
        goto cleanup;
    }

    chunked = is_chunked_encoding(headers);
    (void)parse_content_length(headers, &content_len, &cl_found);

    body_sep = strstr(headers, "\r\n\r\n");
    if (body_sep == NULL) {
        goto cleanup;
    }
    body_sep += 4U;
    body_prefix = header_total - (size_t)(body_sep - headers);

    memset(&src, 0, sizeof(src));
    src.pfx = (const unsigned char *)body_sep;
    src.pfx_len = body_prefix;
    src.pfx_pos = 0U;
    src.conn = &conn;

    out = fopen(tmp_path, "wb");
    if (out == NULL) {
        goto cleanup;
    }

    if (chunked != 0) {
        if (read_body_chunked_src(&src, out, on_progress, progress_user) != 0) {
            goto cleanup;
        }
    } else if (cl_found != 0) {
        if (read_body_known_src(&src, out, content_len, on_progress, progress_user, content_len) != 0) {
            goto cleanup;
        }
    } else {
        if (read_body_until_close_src(&src, out, on_progress, progress_user, 0ULL) != 0) {
            goto cleanup;
        }
    }

    if (fclose(out) != 0) {
        out = NULL;
        goto cleanup;
    }
    out = NULL;

    if (platform_fs_move_path(tmp_path, dest_path) != PLATFORM_OK) {
        goto cleanup;
    }

    rc = 0;

cleanup:
    if (out != NULL) {
        (void)fclose(out);
    }
    conn_close(&conn);
    if (res != NULL) {
        freeaddrinfo(res);
    }
    free(headers);
    if (rc != 0) {
        (void)platform_fs_remove_path(tmp_path);
    }
    free(tmp_path);
    return rc;
}

/* ── in-memory fetch ────────────────────────────────────────────── */

#define UPDATE_HTTP_FETCH_MAX (512U * 1024U)

static int mem_append(char **buf, size_t *len, size_t *cap, const void *chunk, size_t chunk_len)
{
    if (append_grow(buf, len, cap, chunk, chunk_len) != 0) {
        return -1;
    }
    if (*len > UPDATE_HTTP_FETCH_MAX) {
        return -1;
    }
    return 0;
}

static int read_body_known_src_mem(io_src *s,
    char **buf,
    size_t *len,
    size_t *cap,
    unsigned long long nbytes,
    update_http_progress_fn on_progress,
    void *progress_user,
    unsigned long long total_hint)
{
    unsigned char tmp[8192];
    unsigned long long done = 0ULL;

    while (done < nbytes) {
        unsigned long long want = nbytes - done;
        size_t chunk = want > sizeof(tmp) ? sizeof(tmp) : (size_t)want;
        int rd = src_read(s, tmp, chunk);
        if (rd <= 0) {
            return -1;
        }
        if (mem_append(buf, len, cap, tmp, (size_t)rd) != 0) {
            return -1;
        }
        done += (unsigned long long)(size_t)rd;
        if (on_progress != NULL) {
            on_progress(done, total_hint, progress_user);
        }
    }

    return 0;
}

static int read_body_chunked_src_mem(io_src *s,
    char **buf,
    size_t *len,
    size_t *cap,
    update_http_progress_fn on_progress,
    void *progress_user)
{
    unsigned char tmp[8192];
    unsigned long long done = 0ULL;

    for (;;) {
        char line[96];
        unsigned long long chunk_sz;
        unsigned long long i;

        if (read_line_src(s, line, sizeof(line)) != 0) {
            return -1;
        }
        chunk_sz = strtoull(line, NULL, 16);
        if (chunk_sz == 0ULL) {
            if (read_line_src(s, line, sizeof(line)) != 0) {
                return -1;
            }
            break;
        }

        for (i = 0ULL; i < chunk_sz;) {
            size_t want = (size_t)(chunk_sz - i);
            if (want > sizeof(tmp)) {
                want = sizeof(tmp);
            }
            {
                int rd = src_read(s, tmp, want);
                if (rd <= 0) {
                    return -1;
                }
                if (mem_append(buf, len, cap, tmp, (size_t)rd) != 0) {
                    return -1;
                }
                i += (unsigned long long)(size_t)rd;
                done += (unsigned long long)(size_t)rd;
                if (on_progress != NULL) {
                    on_progress(done, 0ULL, progress_user);
                }
            }
        }

        if (read_line_src(s, line, sizeof(line)) != 0) {
            return -1;
        }
    }

    return 0;
}

static int read_body_until_close_src_mem(io_src *s,
    char **buf,
    size_t *len,
    size_t *cap,
    update_http_progress_fn on_progress,
    void *progress_user,
    unsigned long long total_hint)
{
    unsigned char tmp[8192];
    unsigned long long done = 0ULL;

    for (;;) {
        int rd = src_read(s, tmp, sizeof(tmp));
        if (rd < 0) {
            return -1;
        }
        if (rd == 0) {
            break;
        }
        if (mem_append(buf, len, cap, tmp, (size_t)rd) != 0) {
            return -1;
        }
        done += (unsigned long long)(size_t)rd;
        if (on_progress != NULL) {
            on_progress(done, total_hint, progress_user);
        }
    }

    return 0;
}

int update_http_fetch(const char *url,
    char **out_body,
    size_t *out_len,
    update_http_progress_fn on_progress,
    void *progress_user)
{
    char host[512];
    char path[4096];
    unsigned port = 80U;
    int use_tls = 0;
    char portbuf[16];
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    conn_t conn;
    char req[8192];
    int req_len;
    char *headers = NULL;
    size_t header_total = 0U;
    const char *body_sep;
    size_t body_prefix;
    int cl_found = 0;
    unsigned long long content_len = 0ULL;
    int chunked = 0;
    io_src src;
    char *body = NULL;
    size_t blen = 0U;
    size_t bcap = 0U;
    int rc = -1;

    if (url == NULL || out_body == NULL || out_len == NULL) {
        return -1;
    }

    conn_init(&conn);

    *out_body = NULL;
    *out_len = 0U;

    if (parse_http_url(url, host, sizeof(host), path, sizeof(path), &use_tls, &port) != 0) {
        return -1;
    }

    (void)snprintf(portbuf, sizeof(portbuf), "%u", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, portbuf, &hints, &res) != 0) {
        return -1;
    }

    if (conn_connect(&conn, res, host, use_tls) != 0) {
        goto cleanup;
    }

    req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "User-Agent: libupdate/1.0\r\n"
        "Accept: application/json\r\n"
        "\r\n",
        path,
        host);
    if (req_len <= 0 || (size_t)req_len >= sizeof(req)) {
        goto cleanup;
    }

    if (conn_send(&conn, req, (size_t)req_len) != 0) {
        goto cleanup;
    }

    if (read_http_headers(&conn, &headers, &header_total) != 0) {
        goto cleanup;
    }

    if (parse_status_200(headers) != 0) {
        goto cleanup;
    }

    chunked = is_chunked_encoding(headers);
    (void)parse_content_length(headers, &content_len, &cl_found);

    body_sep = strstr(headers, "\r\n\r\n");
    if (body_sep == NULL) {
        goto cleanup;
    }
    body_sep += 4U;
    body_prefix = header_total - (size_t)(body_sep - headers);

    memset(&src, 0, sizeof(src));
    src.pfx = (const unsigned char *)body_sep;
    src.pfx_len = body_prefix;
    src.pfx_pos = 0U;
    src.conn = &conn;

    if (chunked != 0) {
        if (read_body_chunked_src_mem(&src, &body, &blen, &bcap, on_progress, progress_user) != 0) {
            goto cleanup;
        }
    } else if (cl_found != 0) {
        if (read_body_known_src_mem(&src, &body, &blen, &bcap, content_len, on_progress, progress_user, content_len)
            != 0) {
            goto cleanup;
        }
    } else {
        if (read_body_until_close_src_mem(&src, &body, &blen, &bcap, on_progress, progress_user, 0ULL) != 0) {
            goto cleanup;
        }
    }

    *out_body = body;
    *out_len = blen;
    body = NULL;
    rc = 0;

cleanup:
    free(body);
    conn_close(&conn);
    if (res != NULL) {
        freeaddrinfo(res);
    }
    free(headers);
    return rc;
}
