#ifndef IMG3_H
#define IMG3_H

/* ============================================================
 * img3.h — Apple IMG3 firmware image format
 *
 * IMG3 is used by iOS 1.x–9.x for bootloader/kernel images.
 * Container structure:
 *   img3_header_t
 *   [img3_tag_t + data] × N   (VERS, SEPO, KBAG, DATA, CERT...)
 *
 * Decryption flow:
 *   1. Parse KBAG tag → get AES key+IV (encrypted under GID key)
 *   2. Decrypt KBAG with device GID key → plaintext AES key+IV
 *   3. Decrypt DATA tag with plaintext key+IV
 *   (Keys for older firmware are public on theiphonewiki.com)
 * ============================================================ */

#include <stdint.h>
#include <stddef.h>

/* Tag magic values (stored little-endian, shown here as BE) */
#define IMG3_MAGIC        0x496d6733u  /* 'Img3' */
#define IMG3_TAG_DATA     0x41544144u  /* 'DATA' */
#define IMG3_TAG_KBAG     0x4741424bu  /* 'KBAG' */
#define IMG3_TAG_VERS     0x53524556u  /* 'VERS' */
#define IMG3_TAG_SEPO     0x4f504553u  /* 'SEPO' */
#define IMG3_TAG_CERT     0x54524543u  /* 'CERT' */
#define IMG3_TAG_ECID     0x44494345u  /* 'ECID' */
#define IMG3_TAG_SHSH     0x48534853u  /* 'SHSH' */
#define IMG3_TAG_TYPE     0x45505954u  /* 'TYPE' */
#define IMG3_TAG_PROD     0x444f5250u  /* 'PROD' */

/* AES type field in KBAG */
#define IMG3_AES_128      0x80u
#define IMG3_AES_192      0xC0u
#define IMG3_AES_256      0x100u

/* KBAG key_state */
#define IMG3_KBAG_PLAIN   0u  /* plain (for GID decrypt) */
#define IMG3_KBAG_ENC     1u  /* encrypted with UID key  */

#pragma pack(push, 1)

typedef struct {
    uint32_t magic;        /* IMG3_MAGIC */
    uint32_t full_size;    /* total file size */
    uint32_t data_size;    /* size of data portion (excl. header+SHSH) */
    uint32_t shsh_offset;  /* offset from end of data_size to SHSH block */
    uint32_t name;         /* image type tag (e.g. 'ibot', 'ibss') */
} img3_header_t;

typedef struct {
    uint32_t magic;
    uint32_t full_size;    /* includes header+padding */
    uint32_t data_size;    /* actual payload bytes */
    /* data_size bytes of payload follow, then padding to 4-byte align */
} img3_tag_t;

typedef struct {
    uint32_t key_state;
    uint32_t aes_type;
    uint8_t  iv[16];
    uint8_t  key[32];      /* only aes_type/8 bytes are meaningful */
} img3_kbag_data_t;

#pragma pack(pop)

/* Parsed IMG3 file */
typedef struct {
    const uint8_t     *raw;        /* original buffer (caller owns) */
    size_t             raw_size;
    img3_header_t      header;
    /* Pointers into raw: */
    const uint8_t     *data_enc;   /* encrypted DATA payload */
    size_t             data_enc_sz;
    uint8_t           *data_dec;   /* decrypted DATA (heap, free with img3_free) */
    img3_kbag_data_t   kbag;
    int                has_kbag;
    int                decrypted;
} img3_t;

/* ── API ──────────────────────────────────────────────────── */

/* Parse IMG3 from buffer (does NOT copy buf — caller must keep it alive).
 * Returns 0 on success, -1 on error. */
int img3_open(img3_t *out, const uint8_t *buf, size_t len);

/* Decrypt DATA using explicit hex key + IV strings (public keys from wiki).
 * key_hex: 32 or 64 hex chars (AES-128 or AES-256)
 * iv_hex:  32 hex chars
 * Returns 0 on success. Allocates out->data_dec. */
int img3_decrypt_keys(img3_t *img, const char *key_hex, const char *iv_hex);

/* Decrypt KBAG using the device GID key via IOKit (requires tfp0 or hgsp4).
 * Fills img->kbag with plaintext key+IV, does NOT decrypt DATA.
 * Returns 0 on success. */
int img3_decrypt_kbag_gid(img3_t *img);

/* Free resources allocated by img3_open / img3_decrypt_* */
void img3_free(img3_t *img);

/* Write decrypted IMG3 (with DATA replaced) to file.
 * Returns 0 on success. */
int img3_write_decrypted(const img3_t *img, const char *path);

/* Hex string helpers */
int  hex_to_bytes(const char *hex, uint8_t *out, size_t max_bytes);
void bytes_to_hex(const uint8_t *in, size_t len, char *out_hex);

#endif /* IMG3_H */
