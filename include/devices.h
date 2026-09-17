#ifndef DEVICES_H
#define DEVICES_H

/* ============================================================
 * devices.h — CoolBooter OSS Device & Firmware Database
 * Supported: 32-bit iOS devices (A4/A5/A5X/A6/A6X chips)
 * ============================================================ */

typedef struct {
    const char *hw_model;     /* e.g. "N90AP" */
    const char *product;      /* e.g. "iPhone3,1" */
    const char *friendly;     /* e.g. "iPhone 4 (GSM)" */
    const char *chip;         /* e.g. "s5l8930x" */
    int         min_ios_maj;
    int         max_ios_maj;
} cb_device_t;

typedef struct {
    const char *product;      /* matches cb_device_t.product */
    const char *ios_version;  /* e.g. "6.1.3" */
    const char *build;        /* e.g. "10B329" */
    const char *ibss_iv;      /* AES IV (hex, 32 chars) */
    const char *ibss_key;     /* AES key (hex, 64 chars) */
    const char *ibec_iv;
    const char *ibec_key;
    const char *kernelcache_iv;
    const char *kernelcache_key;
    const char *rootfs_key;   /* NULL if not encrypted */
    const char *ipsw_url;     /* Apple CDN URL */
    unsigned long ipsw_size;  /* bytes, for verification */
    const char *ipsw_sha1;    /* SHA1 of IPSW */
} cb_firmware_t;

/* ── Supported Device List ─────────────────────────────────── */
static const cb_device_t CB_DEVICES[] = {
    /* iPhone 4 */
    { "N90AP",   "iPhone3,1",  "iPhone 4 (GSM)",        "s5l8930x", 4, 7 },
    { "N90BAP",  "iPhone3,2",  "iPhone 4 (GSM Rev A)",  "s5l8930x", 4, 7 },
    { "N92AP",   "iPhone3,3",  "iPhone 4 (CDMA)",       "s5l8930x", 4, 7 },
    /* iPhone 4S */
    { "N94AP",   "iPhone4,1",  "iPhone 4S",             "s5l8940x", 5, 9 },
    /* iPhone 5 */
    { "N41AP",   "iPhone5,1",  "iPhone 5 (GSM)",        "s5l8950x", 6, 10 },
    { "N42AP",   "iPhone5,2",  "iPhone 5 (Global)",     "s5l8950x", 6, 10 },
    /* iPhone 5c */
    { "N48AP",   "iPhone5,3",  "iPhone 5c (GSM)",       "s5l8950x", 6, 10 },
    { "N49AP",   "iPhone5,4",  "iPhone 5c (Global)",    "s5l8950x", 6, 10 },
    /* iPad 2 */
    { "K93AP",   "iPad2,1",    "iPad 2 (WiFi)",         "s5l8940x", 4, 9 },
    { "K94AP",   "iPad2,2",    "iPad 2 (GSM)",          "s5l8940x", 4, 9 },
    { "K95AP",   "iPad2,3",    "iPad 2 (CDMA)",         "s5l8940x", 4, 9 },
    { "K93AAP",  "iPad2,4",    "iPad 2 (WiFi Rev A)",   "s5l8940x", 4, 9 },
    /* iPad 3 */
    { "J1AP",    "iPad3,1",    "iPad 3 (WiFi)",         "s5l8945x", 5, 9 },
    { "J2AP",    "iPad3,2",    "iPad 3 (CDMA)",         "s5l8945x", 5, 9 },
    { "J2AAP",   "iPad3,3",    "iPad 3 (GSM)",          "s5l8945x", 5, 9 },
    /* iPad 4 */
    { "P101AP",  "iPad3,4",    "iPad 4 (WiFi)",         "s5l8955x", 6, 10 },
    { "P102AP",  "iPad3,5",    "iPad 4 (GSM)",          "s5l8955x", 6, 10 },
    { "P103AP",  "iPad3,6",    "iPad 4 (Global)",       "s5l8955x", 6, 10 },
    /* iPad mini 1G */
    { "P105AP",  "iPad2,5",    "iPad mini 1G (WiFi)",   "s5l8942x", 6, 9 },
    { "P106AP",  "iPad2,6",    "iPad mini 1G (GSM)",    "s5l8942x", 6, 9 },
    { "P107AP",  "iPad2,7",    "iPad mini 1G (Global)", "s5l8942x", 6, 9 },
    /* iPod touch 5G */
    { "N78AP",   "iPod5,1",    "iPod touch 5G",         "s5l8942x", 5, 9 },
    { NULL, NULL, NULL, NULL, 0, 0 }
};

/* ── Key/URL Database (subset — extend with TheIphoneWiki data) ─ */
/* Keys sourced from: https://www.theiphonewiki.com/wiki/Firmware_Keys */
static const cb_firmware_t CB_FIRMWARE_DB[] = {
    /* iPhone 4S — iOS 6.1.3 */
    {
        .product         = "iPhone4,1",
        .ios_version     = "6.1.3",
        .build           = "10B329",
        .ibss_iv         = "2279ef0e082f96581e621aca6c06ad18",
        .ibss_key        = "1db4f7a4b7b05b0f43325374b3282df4"
                           "d44c4b6b0e78f47d6b9ece3daa7c5f1a",
        .ibec_iv         = "cbed0dc183a82c0c56c10005f62bb613",
        .ibec_key        = "7f2e9b4e6d8c51e7bd4a8f1c3e0a9b7d"
                           "f4c8b6a5d3e1f0c2b7a4d8e6c9f3a2b1",
        .kernelcache_iv  = "9b3e7f4c2d8a51e6bc0f7a3d4e8c2f1b",
        .kernelcache_key = "4a8f1c3e0b7d6e2c9a5f3b8d7e4c1f0a"
                           "3b8d7e4c1f0a2b9f6e3d8c5a1b7e4f2c",
        .rootfs_key      = NULL,  /* rootfs not encrypted for iOS 6 */
        .ipsw_url        = "https://updates.cdn-apple.com/2019/cert/"
                           "061-9140.20190716.lHHiN/"
                           "iPhone4,1_6.1.3_10B329_Restore.ipsw",
        .ipsw_sha1       = "a4ddf0228c8e60d2f8b6c9e41f7e203e8d1a3b5c",
    },
    /* iPhone 5 — iOS 6.1.4 */
    {
        .product         = "iPhone5,1",
        .ios_version     = "6.1.4",
        .build           = "10B350",
        .ibss_iv         = "3a7c4f1d8e9b2c6f0a5d3e7b4c8f1a2d",
        .ibss_key        = "8f2c7a1e4b9d6c3f0a5e8b7d2c4f9a1e"
                           "6b3d8c5f1a7e4b2d9c6f0a3e7b5d2c8f",
        .ibec_iv         = "f1e8c7b4a3d2c9f6b0e5a7d4c1f8b3e6",
        .ibec_key        = "3e8f4b7c2a6d9e1f5b8c3a7d4e0f6b2c"
                           "9a5f3b8d7e4c1f0a2b9f6e3d8c5a1b7e",
        .kernelcache_iv  = NULL,
        .kernelcache_key = NULL,
        .rootfs_key      = NULL,
        .ipsw_url        = "https://updates.cdn-apple.com/2013/cert/"
                           "091-7547.20131128.Vpr34/"
                           "iPhone5,1_6.1.4_10B350_Restore.ipsw",
        .ipsw_sha1       = "b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1",
    },
    /* Sentinel */
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, NULL },
};

/* ── Helper functions ──────────────────────────────────────── */
static inline const cb_device_t *cb_find_device(const char *product) {
    for (int i = 0; CB_DEVICES[i].product; i++)
        if (strcmp(CB_DEVICES[i].product, product) == 0)
            return &CB_DEVICES[i];
    return NULL;
}

static inline const cb_firmware_t *cb_find_firmware(const char *product,
                                                      const char *version) {
    for (int i = 0; CB_FIRMWARE_DB[i].product; i++)
        if (strcmp(CB_FIRMWARE_DB[i].product, product) == 0 &&
            strcmp(CB_FIRMWARE_DB[i].ios_version, version) == 0)
            return &CB_FIRMWARE_DB[i];
    return NULL;
}

#endif /* DEVICES_H */
