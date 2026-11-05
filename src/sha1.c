/*
 * sha1.c - small SHA-1 implementation (public domain style), used only to
 * identify the player's ROM file.
 */
#include <string.h>
#include "sha1.h"

#define ROL(v, b) (((v) << (b)) | ((v) >> (32 - (b))))

static void sha1_block(Sha1 *s, const uint8_t *p)
{
  uint32_t w[80], a, b, c, d, e, f, k, t;
  int i;

  for (i = 0; i < 16; i++)
    w[i] = (uint32_t)p[i*4] << 24 | (uint32_t)p[i*4+1] << 16 | (uint32_t)p[i*4+2] << 8 | p[i*4+3];
  for (i = 16; i < 80; i++)
    w[i] = ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

  a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4];
  for (i = 0; i < 80; i++)
  {
    if      (i < 20) { f = (b & c) | (~b & d);          k = 0x5A827999; }
    else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
    else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
    t = ROL(a, 5) + f + e + k + w[i];
    e = d; d = c; c = ROL(b, 30); b = a; a = t;
  }
  s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}

void sha1_init(Sha1 *s)
{
  s->h[0] = 0x67452301; s->h[1] = 0xEFCDAB89; s->h[2] = 0x98BADCFE;
  s->h[3] = 0x10325476; s->h[4] = 0xC3D2E1F0;
  s->len = 0;
  s->used = 0;
}

void sha1_update(Sha1 *s, const void *data, size_t n)
{
  const uint8_t *p = (const uint8_t *)data;
  s->len += n;
  while (n)
  {
    size_t take = 64 - s->used;
    if (take > n) take = n;
    memcpy(s->buf + s->used, p, take);
    s->used += take; p += take; n -= take;
    if (s->used == 64) { sha1_block(s, s->buf); s->used = 0; }
  }
}

void sha1_final(Sha1 *s, uint8_t out[20])
{
  uint64_t bits = s->len * 8;
  uint8_t pad = 0x80;
  int i;

  sha1_update(s, &pad, 1);
  pad = 0;
  while (s->used != 56) sha1_update(s, &pad, 1);
  for (i = 7; i >= 0; i--)
  {
    uint8_t byte = (uint8_t)(bits >> (i * 8));
    sha1_update(s, &byte, 1);
  }
  for (i = 0; i < 5; i++)
  {
    out[i*4]   = (uint8_t)(s->h[i] >> 24);
    out[i*4+1] = (uint8_t)(s->h[i] >> 16);
    out[i*4+2] = (uint8_t)(s->h[i] >> 8);
    out[i*4+3] = (uint8_t)(s->h[i]);
  }
}

void sha1_hex(const uint8_t digest[20], char out[41])
{
  static const char hex[] = "0123456789abcdef";
  int i;
  for (i = 0; i < 20; i++)
  {
    out[i*2]   = hex[digest[i] >> 4];
    out[i*2+1] = hex[digest[i] & 15];
  }
  out[40] = 0;
}
