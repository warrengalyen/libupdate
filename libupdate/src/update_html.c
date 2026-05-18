#include "update_html.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *d;
    size_t len;
    size_t cap;
    size_t max_total;
} Buf;

typedef enum {
    TID_B = 0,
    TID_I,
    TID_U,
    TID_BR,
    TID_P,
    TID_UL,
    TID_OL,
    TID_LI,
    TID_A,
    TID_SPAN,
    TID__COUNT
} Tid;

static const char *const s_tag_open[TID__COUNT] = {"b", "i", "u", "br", "p", "ul", "ol", "li", "a", "span"};

static int buf_init(Buf *b, size_t max_total)
{
    const size_t init_cap = 4096U;
    size_t first;

    if (b == NULL || max_total == 0U) {
        return -1;
    }
    b->len = 0U;
    b->max_total = max_total;
    first = init_cap < max_total ? init_cap : max_total;
    b->d = (char *)malloc(first);
    if (b->d == NULL) {
        return -1;
    }
    b->cap = first;
    b->d[0] = '\0';
    return 0;
}

static void buf_free(Buf *b)
{
    if (b == NULL) {
        return;
    }
    free(b->d);
    b->d = NULL;
    b->len = 0U;
    b->cap = 0U;
}

static int buf_ensure(Buf *b, size_t add)
{
    size_t need;
    size_t ncap;

    if (b == NULL) {
        return -1;
    }
    if (add > b->max_total - b->len - 1U) {
        return -1;
    }
    need = b->len + add + 1U;
    if (need <= b->cap) {
        return 0;
    }
    ncap = b->cap;
    while (ncap < need && ncap < b->max_total) {
        ncap *= 2U;
        if (ncap == 0U) {
            return -1;
        }
    }
    if (ncap > b->max_total) {
        ncap = b->max_total;
    }
    if (ncap < need) {
        return -1;
    }
      {
        char *nd = (char *)realloc(b->d, ncap);
        if (nd == NULL) {
            return -1;
        }
        b->d = nd;
        b->cap = ncap;
    }
    return 0;
}

static int buf_append(Buf *b, const char *s, size_t n)
{
    if (buf_ensure(b, n) != 0) {
        return -1;
    }
    memcpy(b->d + b->len, s, n);
    b->len += n;
    b->d[b->len] = '\0';
    return 0;
}

static int buf_append_cstr(Buf *b, const char *s)
{
    if (s == NULL) {
        return -1;
    }
    return buf_append(b, s, strlen(s));
}

static unsigned char ascii_lower_uc(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (unsigned char)(c - 'A' + 'a');
    }
    return c;
}

static int tag_name_eq_lc(const char *s, size_t slen, const char *lit_lc)
{
    size_t i;
    size_t ll = strlen(lit_lc);

    if (slen != ll) {
        return 0;
    }
    for (i = 0U; i < slen; i++) {
        if (ascii_lower_uc((unsigned char)s[i]) != (unsigned char)lit_lc[i]) {
            return 0;
        }
    }
    return 1;
}

static int is_tagname_char(unsigned char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return 1;
    }
    if (c == '-') {
        return 1;
    }
    if (c == ':') {
        return 1;
    } /* drop xmlns-style; see dangerous */
    return 0;
}

static const char *skip_ws(const char *p, const char *end)
{
    while (p < end && isspace((unsigned char)*p) != 0) {
        p++;
    }
    return p;
}

static const char *skip_to_tag_end(const char *p, const char *end)
{
    int dq = 0;
    int sq = 0;

    while (p < end) {
        if (dq != 0) {
            if (*p == '"') {
                dq = 0;
            }
            p++;
            continue;
        }
        if (sq != 0) {
            if (*p == '\'') {
                sq = 0;
            }
            p++;
            continue;
        }
        if (*p == '"') {
            dq = 1;
            p++;
            continue;
        }
        if (*p == '\'') {
            sq = 1;
            p++;
            continue;
        }
        if (*p == '>') {
            return p + 1;
        }
        p++;
    }
    return end;
}

static int tag_is_dangerous(const char *name, size_t len)
{
    static const char *danger[] = {"script", "style", "iframe", "img", "video", "audio", "object", "embed",
        "link", "meta", "base", "form", "input", "textarea", "button", "svg", "math",
        NULL};
    size_t i;

    for (i = 0; danger[i] != NULL; i++) {
        if (tag_name_eq_lc(name, len, danger[i]) != 0) {
            return 1;
        }
    }
    return 0;
}

static int classify_tag(const char *name, size_t len, Tid *tid, int *is_void)
{
    *is_void = 0;

    if (tag_name_eq_lc(name, len, "b") != 0) {
        *tid = TID_B;
        return 0;
    }
    if (tag_name_eq_lc(name, len, "i") != 0) {
        *tid = TID_I;
        return 0;
    }
    if (tag_name_eq_lc(name, len, "u") != 0) {
        *tid = TID_U;
        return 0;
    }
    if (tag_name_eq_lc(name, len, "br") != 0) {
        *tid = TID_BR;
        *is_void = 1;
        return 0;
    }
    if (tag_name_eq_lc(name, len, "p") != 0) {
        *tid = TID_P;
        return 0;
    }
    if (tag_name_eq_lc(name, len, "ul") != 0) {
        *tid = TID_UL;
        return 0;
    }
    if (tag_name_eq_lc(name, len, "ol") != 0) {
        *tid = TID_OL;
        return 0;
    }
    if (tag_name_eq_lc(name, len, "li") != 0) {
        *tid = TID_LI;
        return 0;
    }
    if (tag_name_eq_lc(name, len, "a") != 0) {
        *tid = TID_A;
        return 0;
    }
    if (tag_name_eq_lc(name, len, "span") != 0) {
        *tid = TID_SPAN;
        return 0;
    }
    return -1;
}

static const char *find_close_tag_open(const char *p, const char *end, const char *tag_lc)
{
    size_t tl = strlen(tag_lc);

    while (p + 2U + tl <= end) {
        if (*p != '<') {
            p++;
            continue;
        }
        if (p[1] != '/') {
            p++;
            continue;
        }
        if (tag_name_eq_lc(p + 2U, tl, tag_lc) != 0) {
            const char *after = p + 2U + tl;
            if (after >= end || is_tagname_char((unsigned char)*after) == 0) {
                return p;
            }
        }
        p++;
    }
    return NULL;
}

static const char *dangerous_tag_lc(const char *name, size_t len)
{
    static const struct {
        const char *n;
        const char *lc;
    } map[] = {
        {"script", "script"},
        {"style", "style"},
        {"iframe", "iframe"},
        {"img", "img"},
        {"video", "video"},
        {"audio", "audio"},
        {"object", "object"},
        {"embed", "embed"},
        {"link", "link"},
        {"meta", "meta"},
        {"base", "base"},
        {"form", "form"},
        {"input", "input"},
        {"textarea", "textarea"},
        {"button", "button"},
        {"svg", "svg"},
        {"math", "math"},
        {NULL, NULL}};
    size_t i;

    for (i = 0U; map[i].n != NULL; i++) {
        if (tag_name_eq_lc(name, len, map[i].n) != 0) {
            return map[i].lc;
        }
    }
    return NULL;
}

static const char *skip_dangerous_content(const char *p, const char *end, const char *tag_lc)
{
    const char *open = find_close_tag_open(p, end, tag_lc);
    if (open == NULL) {
        return end;
    }
    return skip_to_tag_end(open, end);
}

static int starts_ci(const char *s, size_t n, const char *lit)
{
    size_t i;
    for (i = 0; lit[i] != '\0' && i < n; i++) {
        if (ascii_lower_uc((unsigned char)s[i]) != (unsigned char)lit[i]) {
            return 0;
        }
    }
    return lit[i] == '\0';
}

static int href_is_safe(const char *h, size_t len)
{
    size_t i = 0U;

    while (i < len && isspace((unsigned char)h[i]) != 0) {
        i++;
    }
    if (i >= len) {
        return 0;
    }

    if (h[i] == '#') {
        return 1;
    }

    if (starts_ci(h + i, len - i, "http://") != 0) {
        return 1;
    }
    if (starts_ci(h + i, len - i, "https://") != 0) {
        return 1;
    }
    if (starts_ci(h + i, len - i, "mailto:") != 0) {
        return 1;
    }

    if (starts_ci(h + i, len - i, "javascript:") != 0) {
        return 0;
    }
    if (starts_ci(h + i, len - i, "data:") != 0) {
        return 0;
    }
    if (starts_ci(h + i, len - i, "vbscript:") != 0) {
        return 0;
    }

    return 0;
}

static int emit_html_escaped(Buf *b, const char *s, size_t n)
{
    size_t i;
    for (i = 0U; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '&') {
            if (buf_append_cstr(b, "&amp;") != 0) {
                return -1;
            }
        } else if (c == '<') {
            if (buf_append_cstr(b, "&lt;") != 0) {
                return -1;
            }
        } else if (c == '>') {
            if (buf_append_cstr(b, "&gt;") != 0) {
                return -1;
            }
        } else if (c == '"') {
            if (buf_append_cstr(b, "&quot;") != 0) {
                return -1;
            }
        } else {
            if (buf_append(b, (const char *)&s[i], 1U) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int is_attr_name_char(unsigned char c)
{
    if (is_tagname_char(c) != 0) {
        return 1;
    }
    if (c == '_') {
        return 1;
    }
    return 0;
}

static int attr_name_is_danger(const char *name, size_t nl)
{
    size_t j;
    if (nl >= 2U && ascii_lower_uc((unsigned char)name[0]) == 'o' && ascii_lower_uc((unsigned char)name[1]) == 'n') {
        return 1;
    }
    for (j = 0U; j < nl; j++) {
        if (name[j] == '(') {
            return 1;
        }
    }
    return 0;
}

static const char *skip_attr_value(const char *p, const char *end)
{
    if (p >= end) {
        return end;
    }
    if (*p == '"' || *p == '\'') {
        char q = *p;
        p++;
        while (p < end) {
            if (*p == q) {
                return p + 1;
            }
            p++;
        }
        return end;
    }
    while (p < end && isspace((unsigned char)*p) == 0 && *p != '>') {
        if (*p == '/') {
            break;
        }
        p++;
    }
    return p;
}

/* Parse <a ...>: fill href_out with first safe href (NUL-terminated). */
static void parse_anchor_href(const char *p, const char *end, char *href_out, size_t href_cap)
{
    href_out[0] = '\0';
    p = skip_ws(p, end);
    while (p < end) {
        const char *name_start;
        size_t nl;

        p = skip_ws(p, end);
        if (p >= end) {
            break;
        }
        if (*p == '>') {
            break;
        }
        if (*p == '/' && p + 1U < end && p[1] == '>') {
            break;
        }

        name_start = p;
        nl = 0U;
        while (p < end && is_attr_name_char((unsigned char)*p) != 0) {
            nl++;
            p++;
        }
        p = skip_ws(p, end);
        if (p >= end || *p != '=') {
            continue;
        }
        p = skip_ws(p + 1, end);
        if (p >= end) {
            break;
        }

        if (nl > 0U && attr_name_is_danger(name_start, nl) != 0) {
            p = skip_attr_value(p, end);
            continue;
        }

        if (nl == 4U && tag_name_eq_lc(name_start, nl, "href") != 0) {
            const char *vstart;
            size_t vl;

            if (href_out[0] != '\0') {
                p = skip_attr_value(p, end);
                continue;
            }
            vstart = p;
            vl = 0U;
            if (p < end && (*p == '"' || *p == '\'')) {
                char q = *p;

                p++;
                vstart = p;
                while (p < end && *p != q) {
                    vl++;
                    p++;
                }
                if (p < end && *p == q) {
                    p++;
                }
            } else {
                vstart = p;
                while (p < end && isspace((unsigned char)*p) == 0 && *p != '>' && *p != '/') {
                    vl++;
                    p++;
                }
            }
            if (href_is_safe(vstart, vl) != 0 && vl + 1U <= href_cap) {
                memcpy(href_out, vstart, vl);
                href_out[vl] = '\0';
            }
            continue;
        }

        p = skip_attr_value(p, end);
    }
}

/* 0 = emitted, 1 = skip tag (e.g. <a> with no safe href), -1 = error */
static int buf_emit_open_tag(Buf *b, Tid tid, const char *inner_start, const char *inner_end, int is_void_tag)
{
    char href_buf[2048];

    if (is_void_tag != 0 && tid == TID_BR) {
        return buf_append_cstr(b, "<br>") != 0 ? -1 : 0;
    }

    if (tid == TID_A) {
        parse_anchor_href(inner_start, inner_end, href_buf, sizeof href_buf);
        if (href_buf[0] == '\0') {
            return 1;
        }
        if (buf_append_cstr(b, "<a href=\"") != 0) {
            return -1;
        }
        if (emit_html_escaped(b, href_buf, strlen(href_buf)) != 0) {
            return -1;
        }
        return buf_append_cstr(b, "\">") != 0 ? -1 : 0;
    }

    if (buf_append_cstr(b, "<") != 0) {
        return -1;
    }
    if (buf_append_cstr(b, s_tag_open[tid]) != 0) {
        return -1;
    }
    return buf_append_cstr(b, ">") != 0 ? -1 : 0;
}

static int buf_emit_close_tag(Buf *b, Tid tid)
{
    if (tid == TID_BR) {
        return 0;
    }
    if (buf_append_cstr(b, "</") != 0) {
        return -1;
    }
    if (buf_append_cstr(b, s_tag_open[tid]) != 0) {
        return -1;
    }
    return buf_append_cstr(b, ">") != 0 ? -1 : 0;
}

int update_html_escape_plain_alloc(const char *in, size_t in_len, char **out, size_t max_out_total)
{
    Buf b;
    size_t i;

    if (out == NULL || max_out_total == 0U) {
        return -1;
    }
    *out = NULL;
    if (in == NULL && in_len > 0U) {
        return -1;
    }
    if (in_len > UPDATE_HTML_DESC_MAX_IN) {
        return -1;
    }
    if (buf_init(&b, max_out_total) != 0) {
        return -1;
    }

    if (in != NULL) {
        for (i = 0U; i < in_len; i++) {
            unsigned char c = (unsigned char)in[i];
            if (c == '&') {
                if (buf_append_cstr(&b, "&amp;") != 0) {
                    goto fail;
                }
            } else if (c == '<') {
                if (buf_append_cstr(&b, "&lt;") != 0) {
                    goto fail;
                }
            } else if (c == '>') {
                if (buf_append_cstr(&b, "&gt;") != 0) {
                    goto fail;
                }
            } else if (c == '"') {
                if (buf_append_cstr(&b, "&quot;") != 0) {
                    goto fail;
                }
            } else {
                if (buf_append(&b, (const char *)&in[i], 1U) != 0) {
                    goto fail;
                }
            }
        }
    }

    *out = b.d;
    return 0;

fail:
    buf_free(&b);
    return -1;
}

int update_html_sanitize_alloc(const char *in, size_t in_len, char **out, size_t max_out_total)
{
    const char *end;
    const char *p;
    Tid stack[64];
    int sp = 0;
    Buf b;

    if (out == NULL || max_out_total == 0U) {
        return -1;
    }
    *out = NULL;
    if (in == NULL && in_len > 0U) {
        return -1;
    }
    if (in_len > UPDATE_HTML_DESC_MAX_IN) {
        return -1;
    }
    if (buf_init(&b, max_out_total) != 0) {
        return -1;
    }

    p = in;
    end = in + in_len;

    while (p < end) {
        const char *lt = (const char *)memchr(p, '<', (size_t)(end - p));
        const char *chunk_end = lt != NULL ? lt : end;

        if (chunk_end > p) {
            if (emit_html_escaped(&b, p, (size_t)(chunk_end - p)) != 0) {
                goto fail;
            }
        }
        if (lt == NULL) {
            break;
        }
        p = lt;

        if (p + 4U <= end && p[1] == '!' && p[2] == '-' && p[3] == '-') {
            const char *c = p + 4U;
            while (c + 2U < end) {
                if (c[0] == '-' && c[1] == '-' && c[2] == '>') {
                    p = c + 3U;
                    break;
                }
                c++;
            }
            if (c + 2U >= end) {
                p = end;
            }
            continue;
        }

        if (p + 1U < end && (p[1] == '!' || p[1] == '?')) {
            p = skip_to_tag_end(p, end);
            continue;
        }

        if (p + 1U < end && p[1] == '/') {
            const char *q = skip_ws(p + 2U, end);
            const char *name = q;
            size_t nl = 0U;
            Tid tid;
            int is_void;

            while (q < end && is_tagname_char((unsigned char)*q) != 0) {
                nl++;
                q++;
            }
            if (nl == 0U) {
                if (buf_append_cstr(&b, "&lt;") != 0) {
                    goto fail;
                }
                p++;
                continue;
            }
            p = skip_to_tag_end(lt, end);
            if (classify_tag(name, nl, &tid, &is_void) != 0 || tid == TID_BR) {
                continue;
            }
            if (sp > 0 && stack[sp - 1] == tid) {
                if (buf_emit_close_tag(&b, tid) != 0) {
                    goto fail;
                }
                sp--;
            }
            continue;
        }

          {
            const char *tag_end = skip_to_tag_end(lt, end);
            const char *q = p + 1U;
            const char *name;
            size_t nl;
            Tid tid;
            int is_void = 0;
            int eo;

            q = skip_ws(q, end);
            name = q;
            nl = 0U;
            while (q < end && is_tagname_char((unsigned char)*q) != 0) {
                nl++;
                q++;
            }
            if (nl == 0U) {
                if (buf_append_cstr(&b, "&lt;") != 0) {
                    goto fail;
                }
                p++;
                continue;
            }

            if (tag_is_dangerous(name, nl) != 0) {
                const char *tlc = dangerous_tag_lc(name, nl);
                const char *after_open = tag_end;

                if (tlc != NULL) {
                    p = skip_dangerous_content(after_open, end, tlc);
                } else {
                    p = after_open;
                }
                continue;
            }

            if (classify_tag(name, nl, &tid, &is_void) != 0) {
                p = tag_end;
                continue;
            }

            if (is_void != 0 && tid == TID_BR) {
                eo = buf_emit_open_tag(&b, tid, q, tag_end, 1);
                if (eo < 0) {
                    goto fail;
                }
                p = tag_end;
                continue;
            }

            if (sp >= (int)(sizeof stack / sizeof stack[0])) {
                p = tag_end;
                continue;
            }

            eo = buf_emit_open_tag(&b, tid, q, tag_end, 0);
            if (eo < 0) {
                goto fail;
            }
            if (eo == 1) {
                p = tag_end;
                continue;
            }
            stack[sp++] = tid;
            p = tag_end;
        }
    }

    while (sp > 0) {
        Tid t = stack[--sp];
        if (buf_emit_close_tag(&b, t) != 0) {
            goto fail;
        }
    }

    *out = b.d;
    return 0;

fail:
    buf_free(&b);
    return -1;
}
