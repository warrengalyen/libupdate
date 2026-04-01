#include "sha256.h"

#include <string.h>

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32U - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x) (ROTR((x), 2U) ^ ROTR((x), 13U) ^ ROTR((x), 22U))
#define BSIG1(x) (ROTR((x), 6U) ^ ROTR((x), 11U) ^ ROTR((x), 25U))
#define SSIG0(x) (ROTR((x), 7U) ^ ROTR((x), 18U) ^ ((x) >> 3U))
#define SSIG1(x) (ROTR((x), 17U) ^ ROTR((x), 19U) ^ ((x) >> 10U))

static void sha256_transform(uint32_t *state, const uint8_t block[64])
{
    static const uint32_t K[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };

    uint32_t W[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t T1;
    uint32_t T2;
    int t;

    for (t = 0; t < 16; t++) {
        W[t] = ((uint32_t)block[t * 4 + 0] << 24) | ((uint32_t)block[t * 4 + 1] << 16)
            | ((uint32_t)block[t * 4 + 2] << 8) | ((uint32_t)block[t * 4 + 3]);
    }
    for (t = 16; t < 64; t++) {
        W[t] = SSIG1(W[t - 2]) + W[t - 7] + SSIG0(W[t - 15]) + W[t - 16];
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];

    for (t = 0; t < 64; t++) {
        T1 = h + BSIG1(e) + CH(e, f, g) + K[t] + W[t];
        T2 = BSIG0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void sha256_init(sha256_ctx *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->nbit = 0ULL;
    ctx->buflen = 0U;
    ctx->h[0] = 0x6a09e667U;
    ctx->h[1] = 0xbb67ae85U;
    ctx->h[2] = 0x3c6ef372U;
    ctx->h[3] = 0xa54ff53aU;
    ctx->h[4] = 0x510e527fU;
    ctx->h[5] = 0x9b05688cU;
    ctx->h[6] = 0x1f83d9abU;
    ctx->h[7] = 0x5be0cd19U;
}

void sha256_update(sha256_ctx *ctx, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t n;
    size_t space;

    if (ctx == NULL || (len > 0U && p == NULL)) {
        return;
    }

    ctx->nbit += (uint64_t)len * 8ULL;

    while (len > 0U) {
        space = 64U - ctx->buflen;
        n = len < space ? len : space;
        memcpy(ctx->buf + ctx->buflen, p, n);
        ctx->buflen += n;
        p += n;
        len -= n;

        if (ctx->buflen == 64U) {
            sha256_transform(ctx->h, ctx->buf);
            ctx->buflen = 0U;
        }
    }
}

void sha256_final(sha256_ctx *ctx, uint8_t out[32])
{
    uint8_t block[128];
    uint64_t bitlen;
    size_t n;
    size_t i;
    int s;

    if (ctx == NULL || out == NULL) {
        return;
    }

    bitlen = ctx->nbit;
    n = ctx->buflen;

    memcpy(block, ctx->buf, n);
    block[n++] = 0x80U;
    while ((n % 64U) != 56U) {
        if (n >= sizeof(block)) {
            return;
        }
        block[n++] = 0U;
    }

    if (n + 8U > sizeof(block)) {
        return;
    }

    for (s = 56; s >= 0; s -= 8) {
        block[n++] = (uint8_t)((bitlen >> (unsigned)s) & 255ULL);
    }

    for (i = 0U; i < n; i += 64U) {
        sha256_transform(ctx->h, block + i);
    }

    for (i = 0U; i < 8U; i++) {
        out[i * 4U + 0U] = (uint8_t)((ctx->h[i] >> 24) & 255U);
        out[i * 4U + 1U] = (uint8_t)((ctx->h[i] >> 16) & 255U);
        out[i * 4U + 2U] = (uint8_t)((ctx->h[i] >> 8) & 255U);
        out[i * 4U + 3U] = (uint8_t)(ctx->h[i] & 255U);
    }
}
