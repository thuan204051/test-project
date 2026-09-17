#ifndef IPSW_H
#define IPSW_H

/* ============================================================
 * ipsw.h — IPSW firmware download and extraction
 *
 * IPSW files are ZIP archives containing:
 *   Firmware/all_flash/iBSS.<chip>.RELEASE.dfu   (IMG3/IMG4)
 *   Firmware/all_flash/iBEC.<chip>.RELEASE.dfu   (IMG3/IMG4)
 *   kernelcache.release.<board>                   (IMG3/IMG4)
 *   <buildid>.<variant>.dmg                       (root filesystem)
 *   BuildManifest.plist                           (firmware metadata)
 *   Restore.plist                                 (restore info)
 * ============================================================ */

#include <stddef.h>

#define CB_IPSW_CACHE_DIR  "/var/mobile/Media/CoolBooter/ipsw"
#define CB_WORK_DIR        "/var/mobile/Media/CoolBooter"

typedef struct {
    char  path[512];           /* local IPSW file path */
    char  ibss_path[512];      /* extracted iBSS .dfu path */
    char  ibec_path[512];      /* extracted iBEC .dfu path */
    char  kernel_path[512];    /* extracted kernelcache path */
    char  rootfs_path[512];    /* extracted root filesystem .dmg */
    char  version[32];         /* iOS version string */
    char  build[32];           /* build number */
    char  product[64];         /* target product identifier */
    unsigned long rootfs_size; /* rootfs DMG size in bytes */
} ipsw_t;

/* Download IPSW from url to local_path.
 * Shows progress. Returns 0 on success. */
int ipsw_download(const char *url, const char *local_path);

/* Verify IPSW SHA1 hash. Returns 0 if matches, -1 otherwise. */
int ipsw_verify(const char *path, const char *expected_sha1);

/* Extract components from IPSW into output_dir.
 * Populates the ipsw_t struct with paths to extracted files.
 * Returns 0 on success. */
int ipsw_extract(const char *ipsw_path, const char *output_dir, ipsw_t *out);

/* Parse BuildManifest.plist to get component filenames.
 * (Internal helper used by ipsw_extract.) */
int ipsw_parse_manifest(const char *manifest_path, ipsw_t *out);

/* Check if an IPSW is already cached.
 * Returns 1 if cached and valid, 0 otherwise. */
int ipsw_is_cached(const char *product, const char *version);

/* Get the cached IPSW path for product+version. */
int ipsw_get_cached_path(const char *product, const char *version,
                         char *out, size_t out_sz);

#endif /* IPSW_H */
