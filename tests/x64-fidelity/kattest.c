/* kattest: known-answer tests for the instructions whose emulation depends on host crypto/CRC support
 * (SSE4.2 CRC32, AES-NI, PCLMULQDQ, SHA-NI, VAES/VPCLMULQDQ when advertised). Every advertised
 * instruction is compared with a portable C implementation on random inputs. Not advertised: recorded.
 * Build: x86_64-w64-mingw32-clang -O2 -msse4.2 -maes -mpclmul -msha -mavx2 -mvaes -mvpclmulqdq kattest.c */
#include "fid.h"
#include <immintrin.h>

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;
static uint64_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17; return rng_state; }
static void fill(void *p, size_t n) { uint8_t *b = p; while (n--) *b++ = (uint8_t)rnd(); }

static int cpu_bit(unsigned leaf, unsigned sub, int reg, unsigned bit)
{
    int r[4];
    __cpuidex(r, leaf, sub);
    return (r[reg] >> bit) & 1;
}

/* ---------------------------------------------------------------- CRC32C */

static uint32_t crc32c_sw(uint32_t crc, const uint8_t *p, size_t n)
{
    while (n--)
    {
        unsigned k;
        crc ^= *p++;
        for (k = 0; k < 8; k++) crc = crc & 1 ? (crc >> 1) ^ 0x82f63b78 : crc >> 1;
    }
    return crc;
}

__attribute__((target("sse4.2"))) static void test_crc32(void)
{
    unsigned i, bad = 0;
    for (i = 0; i < 20000; i++)
    {
        uint8_t buf[8];
        uint32_t c = (uint32_t)rnd(), w8, w16, w32;
        uint64_t w64;
        fill(buf, 8);
        w8 = _mm_crc32_u8(c, buf[0]);
        w16 = _mm_crc32_u16(c, *(uint16_t *)buf);
        w32 = _mm_crc32_u32(c, *(uint32_t *)buf);
        w64 = _mm_crc32_u64(c, *(uint64_t *)buf);
        if (w8 != crc32c_sw(c, buf, 1) || w16 != crc32c_sw(c, buf, 2) || w32 != crc32c_sw(c, buf, 4) ||
            w64 != crc32c_sw(c, buf, 8)) bad++;
    }
    fid_check(!bad, "kat.crc32", "%u of 20000 wrong", bad);
}

/* ---------------------------------------------------------------- AES */

static uint8_t sbox[256], inv_sbox[256];
static uint8_t xt(uint8_t a) { return (uint8_t)((a << 1) ^ (a & 0x80 ? 0x1b : 0)); }
static uint8_t gmul(uint8_t a, uint8_t b) { uint8_t r = 0; while (b) { if (b & 1) r ^= a; a = xt(a); b >>= 1; } return r; }
static void init_sbox(void)
{
    unsigned i;
    for (i = 0; i < 256; i++)
    {
        uint8_t inv = 0, x, s;
        unsigned j;
        if (i) for (j = 1; j < 256; j++) if (gmul((uint8_t)i, (uint8_t)j) == 1) { inv = (uint8_t)j; break; }
        x = inv; s = inv;
        for (j = 0; j < 4; j++) { x = (uint8_t)((x << 1) | (x >> 7)); s ^= x; }
        sbox[i] = s ^ 0x63;
        inv_sbox[sbox[i]] = (uint8_t)i;
    }
}
/* state as 16 bytes, column-major (Intel's byte order: byte i = row i%4, column i/4) */
static void sub_bytes(uint8_t *s, const uint8_t *box) { unsigned i; for (i = 0; i < 16; i++) s[i] = box[s[i]]; }
static void shift_rows(uint8_t *s, int inv)
{
    uint8_t t[16];
    unsigned r, c;
    for (r = 0; r < 4; r++) for (c = 0; c < 4; c++)
        t[r + 4 * c] = s[r + 4 * (inv ? (c + 4 - r) % 4 : (c + r) % 4)];
    memcpy(s, t, 16);
}
static void mix_columns(uint8_t *s, int inv)
{
    unsigned c;
    for (c = 0; c < 4; c++)
    {
        uint8_t *a = s + 4 * c, b[4];
        if (!inv)
        {
            b[0] = gmul(a[0], 2) ^ gmul(a[1], 3) ^ a[2] ^ a[3];
            b[1] = a[0] ^ gmul(a[1], 2) ^ gmul(a[2], 3) ^ a[3];
            b[2] = a[0] ^ a[1] ^ gmul(a[2], 2) ^ gmul(a[3], 3);
            b[3] = gmul(a[0], 3) ^ a[1] ^ a[2] ^ gmul(a[3], 2);
        }
        else
        {
            b[0] = gmul(a[0], 14) ^ gmul(a[1], 11) ^ gmul(a[2], 13) ^ gmul(a[3], 9);
            b[1] = gmul(a[0], 9) ^ gmul(a[1], 14) ^ gmul(a[2], 11) ^ gmul(a[3], 13);
            b[2] = gmul(a[0], 13) ^ gmul(a[1], 9) ^ gmul(a[2], 14) ^ gmul(a[3], 11);
            b[3] = gmul(a[0], 11) ^ gmul(a[1], 13) ^ gmul(a[2], 9) ^ gmul(a[3], 14);
        }
        memcpy(a, b, 4);
    }
}
static void xor16(uint8_t *s, const uint8_t *k) { unsigned i; for (i = 0; i < 16; i++) s[i] ^= k[i]; }

__attribute__((target("aes,sse4.1"))) static void test_aes(void)
{
    unsigned i, bad[6] = { 0 };
    for (i = 0; i < 5000; i++)
    {
        uint8_t st[16], key[16], ref[16], got[16];
        __m128i S, K;
        unsigned rcon = (unsigned)rnd() & 0xff;
        fill(st, 16); fill(key, 16);
        S = _mm_loadu_si128((__m128i *)st); K = _mm_loadu_si128((__m128i *)key);
        /* AESENC */
        memcpy(ref, st, 16); sub_bytes(ref, sbox); shift_rows(ref, 0); mix_columns(ref, 0); xor16(ref, key);
        _mm_storeu_si128((__m128i *)got, _mm_aesenc_si128(S, K)); if (memcmp(got, ref, 16)) bad[0]++;
        /* AESENCLAST */
        memcpy(ref, st, 16); sub_bytes(ref, sbox); shift_rows(ref, 0); xor16(ref, key);
        _mm_storeu_si128((__m128i *)got, _mm_aesenclast_si128(S, K)); if (memcmp(got, ref, 16)) bad[1]++;
        /* AESDEC */
        memcpy(ref, st, 16); shift_rows(ref, 1); sub_bytes(ref, inv_sbox); mix_columns(ref, 1); xor16(ref, key);
        _mm_storeu_si128((__m128i *)got, _mm_aesdec_si128(S, K)); if (memcmp(got, ref, 16)) bad[2]++;
        /* AESDECLAST */
        memcpy(ref, st, 16); shift_rows(ref, 1); sub_bytes(ref, inv_sbox); xor16(ref, key);
        _mm_storeu_si128((__m128i *)got, _mm_aesdeclast_si128(S, K)); if (memcmp(got, ref, 16)) bad[3]++;
        /* AESIMC */
        memcpy(ref, key, 16); mix_columns(ref, 1);
        _mm_storeu_si128((__m128i *)got, _mm_aesimc_si128(K)); if (memcmp(got, ref, 16)) bad[4]++;
        /* AESKEYGENASSIST: X1 = dword 1, X3 = dword 3 of the source; the immediate is the round constant */
        {
            static const unsigned imms[4] = { 0x00, 0x01, 0x36, 0x80 };
            uint32_t d[4], r[4], imm = imms[rcon & 3];
            __m128i g;
            memcpy(d, st, 16);
#define SUBW(w) ((uint32_t)sbox[(w) & 0xff] | (uint32_t)sbox[((w) >> 8) & 0xff] << 8 | (uint32_t)sbox[((w) >> 16) & 0xff] << 16 | (uint32_t)sbox[(w) >> 24] << 24)
#define ROT(w) (((w) >> 8) | ((w) << 24))
            r[0] = SUBW(d[1]); r[1] = ROT(SUBW(d[1])) ^ imm; r[2] = SUBW(d[3]); r[3] = ROT(SUBW(d[3])) ^ imm;
            switch (rcon & 3)
            {
            case 0: g = _mm_aeskeygenassist_si128(S, 0x00); break;
            case 1: g = _mm_aeskeygenassist_si128(S, 0x01); break;
            case 2: g = _mm_aeskeygenassist_si128(S, 0x36); break;
            default: g = _mm_aeskeygenassist_si128(S, 0x80); break;
            }
            _mm_storeu_si128((__m128i *)got, g);
            if (memcmp(got, r, 16)) bad[5]++;
        }
    }
    fid_check(!bad[0], "kat.aesenc", "%u of 5000 wrong", bad[0]);
    fid_check(!bad[1], "kat.aesenclast", "%u of 5000 wrong", bad[1]);
    fid_check(!bad[2], "kat.aesdec", "%u of 5000 wrong", bad[2]);
    fid_check(!bad[3], "kat.aesdeclast", "%u of 5000 wrong", bad[3]);
    fid_check(!bad[4], "kat.aesimc", "%u of 5000 wrong", bad[4]);
    fid_check(!bad[5], "kat.aeskeygenassist", "%u of 5000 wrong", bad[5]);
}

/* ---------------------------------------------------------------- PCLMULQDQ */

static void clmul_sw(uint64_t a, uint64_t b, uint64_t *lo, uint64_t *hi)
{
    unsigned i;
    *lo = *hi = 0;
    for (i = 0; i < 64; i++)
        if ((b >> i) & 1) { *lo ^= a << i; if (i) *hi ^= a >> (64 - i); }
}

__attribute__((target("pclmul"))) static void test_pclmul(void)
{
    unsigned i, bad = 0;
    for (i = 0; i < 20000; i++)
    {
        uint64_t a[2] = { rnd(), rnd() }, b[2] = { rnd(), rnd() }, g[2], lo, hi;
        __m128i A = _mm_loadu_si128((__m128i *)a), B = _mm_loadu_si128((__m128i *)b);
        _mm_storeu_si128((__m128i *)g, _mm_clmulepi64_si128(A, B, 0x00)); clmul_sw(a[0], b[0], &lo, &hi); if (g[0] != lo || g[1] != hi) bad++;
        _mm_storeu_si128((__m128i *)g, _mm_clmulepi64_si128(A, B, 0x01)); clmul_sw(a[1], b[0], &lo, &hi); if (g[0] != lo || g[1] != hi) bad++;
        _mm_storeu_si128((__m128i *)g, _mm_clmulepi64_si128(A, B, 0x10)); clmul_sw(a[0], b[1], &lo, &hi); if (g[0] != lo || g[1] != hi) bad++;
        _mm_storeu_si128((__m128i *)g, _mm_clmulepi64_si128(A, B, 0x11)); clmul_sw(a[1], b[1], &lo, &hi); if (g[0] != lo || g[1] != hi) bad++;
    }
    fid_check(!bad, "kat.pclmulqdq", "%u of 80000 wrong", bad);
}

/* ---------------------------------------------------------------- SHA-NI: full digests */

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha256_block_sw(uint32_t *h, const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, hh;
    unsigned i;
    for (i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (; i < 64; i++)
        w[i] = w[i - 16] + (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] + (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10));
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (i = 0; i < 64; i++)
    {
        uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

/* Intel's SHA-NI SHA-256 block function (white paper "Intel SHA Extensions", 2013) */
__attribute__((target("sha,sse4.1"))) static void sha256_block_ni(uint32_t *h, const uint8_t *data)
{
    const __m128i MASK = _mm_set_epi64x(0x0c0d0e0f08090a0bull, 0x0405060700010203ull);
    __m128i STATE0, STATE1, MSG, TMP, MSG0, MSG1, MSG2, MSG3, ABEF_SAVE, CDGH_SAVE;
    unsigned i;
    TMP = _mm_loadu_si128((const __m128i *)&h[0]);
    STATE1 = _mm_loadu_si128((const __m128i *)&h[4]);
    TMP = _mm_shuffle_epi32(TMP, 0xB1);
    STATE1 = _mm_shuffle_epi32(STATE1, 0x1B);
    STATE0 = _mm_alignr_epi8(TMP, STATE1, 8);
    STATE1 = _mm_blend_epi16(STATE1, TMP, 0xF0);
    ABEF_SAVE = STATE0; CDGH_SAVE = STATE1;
    MSG0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 0)), MASK);
    MSG1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 16)), MASK);
    MSG2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 32)), MASK);
    MSG3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 48)), MASK);
    for (i = 0; i < 16; i++)
    {
        __m128i W = i == 0 ? MSG0 : i == 1 ? MSG1 : i == 2 ? MSG2 : MSG3;
        if (i >= 4)
        {
            /* schedule: W(i) = msg2(msg1(W(i-4), W(i-3)) + alignr(W(i-1), W(i-2), 4), W(i-1)) */
            __m128i t = _mm_sha256msg1_epu32(MSG0, MSG1);
            t = _mm_add_epi32(t, _mm_alignr_epi8(MSG3, MSG2, 4));
            W = _mm_sha256msg2_epu32(t, MSG3);
            MSG0 = MSG1; MSG1 = MSG2; MSG2 = MSG3; MSG3 = W;
        }
        MSG = _mm_add_epi32(W, _mm_loadu_si128((const __m128i *)&K256[4 * i]));
        STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
        MSG = _mm_shuffle_epi32(MSG, 0x0E);
        STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    }
    STATE0 = _mm_add_epi32(STATE0, ABEF_SAVE);
    STATE1 = _mm_add_epi32(STATE1, CDGH_SAVE);
    TMP = _mm_shuffle_epi32(STATE0, 0x1B);
    STATE1 = _mm_shuffle_epi32(STATE1, 0xB1);
    STATE0 = _mm_blend_epi16(TMP, STATE1, 0xF0);
    STATE1 = _mm_alignr_epi8(STATE1, TMP, 8);
    _mm_storeu_si128((__m128i *)&h[0], STATE0);
    _mm_storeu_si128((__m128i *)&h[4], STATE1);
}

static void sha1_block_sw(uint32_t *h, const uint8_t *p)
{
    uint32_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    unsigned i;
    for (i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (; i < 80; i++) { uint32_t t = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]; w[i] = (t << 1) | (t >> 31); }
    for (i = 0; i < 80; i++)
    {
        uint32_t f, k, t;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else { f = b ^ c ^ d; k = 0xca62c1d6; }
        t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
        e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

/* SHA-1 with SHA-NI, the structure of Intel's reference code: 20 groups of 4 rounds, E0/E1 alternating,
 * W(r) = sha1msg2(sha1msg1(W(r-4), W(r-3)) ^ W(r-2), W(r-1)) for r >= 4 */
__attribute__((target("sha,sse4.1"))) static void sha1_block_ni(uint32_t *h, const uint8_t *data)
{
    const __m128i MASK = _mm_set_epi64x(0x0001020304050607ull, 0x08090a0b0c0d0e0full);
    __m128i ABCD, ABCD_SAVE, E0, E0_SAVE, E1 = _mm_setzero_si128(), W[4], X;
    unsigned r;
    ABCD = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i *)h), 0x1B);
    E0 = _mm_set_epi32((int)h[4], 0, 0, 0);
    ABCD_SAVE = ABCD; E0_SAVE = E0;
    for (r = 0; r < 4; r++) W[r] = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(data + 16 * r)), MASK);
    for (r = 0; r < 20; r++)
    {
        if (r < 4) X = W[r];
        else
        {
            X = _mm_sha1msg2_epu32(_mm_xor_si128(_mm_sha1msg1_epu32(W[0], W[1]), W[2]), W[3]);
            W[0] = W[1]; W[1] = W[2]; W[2] = W[3]; W[3] = X;
        }
#define RNDS4(dst, e) switch (r / 5) { case 0: dst = _mm_sha1rnds4_epu32(dst, e, 0); break; \
        case 1: dst = _mm_sha1rnds4_epu32(dst, e, 1); break; case 2: dst = _mm_sha1rnds4_epu32(dst, e, 2); break; \
        default: dst = _mm_sha1rnds4_epu32(dst, e, 3); break; }
        if (r == 0) { E0 = _mm_add_epi32(E0, X); E1 = ABCD; RNDS4(ABCD, E0) }
        else if (r & 1) { E1 = _mm_sha1nexte_epu32(E1, X); E0 = ABCD; RNDS4(ABCD, E1) }
        else { E0 = _mm_sha1nexte_epu32(E0, X); E1 = ABCD; RNDS4(ABCD, E0) }
#undef RNDS4
    }
    E0 = _mm_sha1nexte_epu32(E0, E0_SAVE);
    ABCD = _mm_add_epi32(ABCD, ABCD_SAVE);
    ABCD = _mm_shuffle_epi32(ABCD, 0x1B);
    _mm_storeu_si128((__m128i *)h, ABCD);
    h[4] = (uint32_t)_mm_extract_epi32(E0, 3);
}

static void test_sha(void)
{
    unsigned i, bad256 = 0, bad1 = 0;
    for (i = 0; i < 3000; i++)
    {
        uint8_t blk[64];
        uint32_t a[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 }, b[8];
        uint32_t c[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 }, d[5];
        fill(blk, 64);
        if (i & 1) { a[0] ^= (uint32_t)rnd(); c[2] ^= (uint32_t)rnd(); }
        memcpy(b, a, sizeof(a)); memcpy(d, c, sizeof(c));
        sha256_block_sw(a, blk); sha256_block_ni(b, blk);
        if (memcmp(a, b, sizeof(a))) bad256++;
        sha1_block_sw(c, blk); sha1_block_ni(d, blk);
        if (memcmp(c, d, sizeof(c))) bad1++;
    }
    fid_check(!bad256, "kat.sha256", "%u of 3000 blocks wrong", bad256);
    fid_check(!bad1, "kat.sha1", "%u of 3000 blocks wrong", bad1);
}

/* ---------------------------------------------------------------- VAES / VPCLMULQDQ: per-lane equality */

__attribute__((target("vaes,avx2,aes"))) static void test_vaes(void)
{
    unsigned i, bad = 0;
    for (i = 0; i < 5000; i++)
    {
        uint8_t s[32], k[32], g[32], r0[16], r1[16];
        fill(s, 32); fill(k, 32);
        _mm256_storeu_si256((__m256i *)g, _mm256_aesenc_epi128(_mm256_loadu_si256((__m256i *)s), _mm256_loadu_si256((__m256i *)k)));
        _mm_storeu_si128((__m128i *)r0, _mm_aesenc_si128(_mm_loadu_si128((__m128i *)s), _mm_loadu_si128((__m128i *)k)));
        _mm_storeu_si128((__m128i *)r1, _mm_aesenc_si128(_mm_loadu_si128((__m128i *)(s + 16)), _mm_loadu_si128((__m128i *)(k + 16))));
        if (memcmp(g, r0, 16) || memcmp(g + 16, r1, 16)) bad++;
    }
    fid_check(!bad, "kat.vaesenc", "%u of 5000 wrong", bad);
}

__attribute__((target("vpclmulqdq,avx2,pclmul"))) static void test_vpclmul(void)
{
    unsigned i, bad = 0;
    for (i = 0; i < 5000; i++)
    {
        uint64_t a[4], b[4], g[4], lo, hi;
        fill(a, 32); fill(b, 32);
        _mm256_storeu_si256((__m256i *)g, _mm256_clmulepi64_epi128(_mm256_loadu_si256((__m256i *)a), _mm256_loadu_si256((__m256i *)b), 0x01));
        clmul_sw(a[1], b[0], &lo, &hi); if (g[0] != lo || g[1] != hi) bad++;
        clmul_sw(a[3], b[2], &lo, &hi); if (g[2] != lo || g[3] != hi) bad++;
    }
    fid_check(!bad, "kat.vpclmulqdq", "%u of 10000 wrong", bad);
}

int main(int argc, char **argv)
{
    int sse42 = cpu_bit(1, 0, 2, 20), aes = cpu_bit(1, 0, 2, 25), pclmul = cpu_bit(1, 0, 2, 1), sha = cpu_bit(7, 0, 1, 29);
    int avx2 = cpu_bit(7, 0, 1, 5), vaes = cpu_bit(7, 0, 2, 9), vpclmul = cpu_bit(7, 0, 2, 10);
    setvbuf(stdout, NULL, _IONBF, 0);
    fid_name = "kattest";
    fid_environment();
    init_sbox();
    fid_info("kat.advertised", "sse4.2 %d aes %d pclmulqdq %d sha %d vaes %d vpclmulqdq %d", sse42, aes, pclmul, sha, vaes, vpclmul);
    if (sse42) test_crc32();
    if (aes) test_aes();
    if (pclmul) test_pclmul();
    if (sha) test_sha();
    if (vaes && avx2 && aes) test_vaes();
    if (vpclmul && avx2 && pclmul) test_vpclmul();
    return fid_summary();
}
