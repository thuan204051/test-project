/* POSIX */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

/* ============================================================
 * keybag.c — Data Protection keybag handling
 *
 * iOS Data Protection ties file encryption keys to a "keybag"
 * stored in Effaceable Storage (a special NAND region that
 * survives OS reinstalls but can be erased on wipe).
 *
 * Problem: When the secondary OS boots, it creates its OWN keybag
 * with different keys. Apps from the primary OS have data encrypted
 * under the PRIMARY keybag and can't be decrypted on secondary.
 *
 * CoolBooter's fix: Copy the primary keybag to the secondary OS
 * so both use the same encryption keys. Both OSes will then share
 * the same passcode/biometric.
 *
 * Keybag locations on iOS:
 *   /private/var/keybags/systembag.kb        ← system keybag
 *   /private/var/keybags/emfkeybag.plist     ← EMF keybag (iOS 10+)
 *
 * Effaceable Storage:
 *   Accessed via IOKit IOFlashController or by reading/writing
 *   /dev/rdisk0 at specific offsets (device-dependent).
 *   On A5+ devices, the keybag is at a reserved LBA near the
 *   end of the NAND, outside the partition table.
 *
 * For CoolBooter's use case, copying the .kb file is sufficient
 * because we're not erasing the device — just dual-booting.
 * The Effaceable Storage hardware copy is only needed for full
 * restore scenarios (CoolBooter does NOT do a full erase restore).
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>

#define KEYBAG_SRC_DIR    "/private/var/keybags"
#define KEYBAG_SYSTEM     KEYBAG_SRC_DIR "/systembag.kb"
#define KEYBAG_EMF        KEYBAG_SRC_DIR "/emfkeybag.plist"
#define KEYBAG_USER       KEYBAG_SRC_DIR "/userbag.kb"

/* ── Helpers ─────────────────────────────────────────────────── */
static int copy_file(const char *src, const char *dst) {
    FILE *sf = fopen(src, "rb");
    if (!sf) return -1;
    FILE *df = fopen(dst, "wb");
    if (!df) { fclose(sf); return -1; }

    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), sf)) > 0)
        fwrite(buf, 1, n, df);

    fclose(sf); fclose(df);
    return 0;
}

static void ensure_dir(const char *path) {
    /* mkdir -p equivalent: try each component */
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* ── Copy keybag to secondary rootfs ─────────────────────────── */
/*
 * secondary_mnt: mount point of secondary OS rootfs
 * e.g. "/var/mobile/Media/CoolBooter/mnt"
 */
int keybag_copy_to_secondary(const char *secondary_mnt) {
    if (!secondary_mnt || secondary_mnt[0] == '\0') return -1;

    struct stat st;
    int copied = 0;

    /* Destination directory */
    char dst_dir[512];
    snprintf(dst_dir, sizeof(dst_dir),
             "%s/private/var/keybags", secondary_mnt);
    ensure_dir(dst_dir);

    /* Copy systembag.kb */
    if (stat(KEYBAG_SYSTEM, &st) == 0) {
        char dst[512];
        snprintf(dst, sizeof(dst), "%s/systembag.kb", dst_dir);
        if (copy_file(KEYBAG_SYSTEM, dst) == 0) {
            fprintf(stderr, "[keybag] Copied systembag.kb → secondary\n");
            copied++;
        } else {
            fprintf(stderr, "[keybag] WARNING: Failed to copy systembag.kb\n");
        }
    }

    /* Copy emfkeybag.plist (iOS 10+) */
    if (stat(KEYBAG_EMF, &st) == 0) {
        char dst[512];
        snprintf(dst, sizeof(dst), "%s/emfkeybag.plist", dst_dir);
        if (copy_file(KEYBAG_EMF, dst) == 0) {
            fprintf(stderr, "[keybag] Copied emfkeybag.plist → secondary\n");
            copied++;
        }
    }

    /* Copy userbag.kb (present on some configurations) */
    if (stat(KEYBAG_USER, &st) == 0) {
        char dst[512];
        snprintf(dst, sizeof(dst), "%s/userbag.kb", dst_dir);
        if (copy_file(KEYBAG_USER, dst) == 0) {
            fprintf(stderr, "[keybag] Copied userbag.kb → secondary\n");
            copied++;
        }
    }

    if (copied == 0) {
        fprintf(stderr, "[keybag] No keybag files found at %s\n",
                KEYBAG_SRC_DIR);
        fprintf(stderr, "  Data protection fix may be incomplete\n");
        return -1;
    }

    /* Also copy the data protection class keys from
     * /private/var/MobileDevice/ProvisioningProfiles and
     * MobileStorageMounter state if present */
    {
        const char *extra_srcs[] = {
            "/private/var/MobileDevice/ProvisioningProfiles",
            NULL
        };
        for (int i = 0; extra_srcs[i]; i++) {
            if (stat(extra_srcs[i], &st) == 0) {
                char cmd[1024];
                char *last_sep = strrchr(extra_srcs[i], '/');
                const char *dirname = last_sep ? last_sep + 1 : extra_srcs[i];
                char dst_parent[512];
                snprintf(dst_parent, sizeof(dst_parent),
                         "%s/private/var/MobileDevice", secondary_mnt);
                ensure_dir(dst_parent);
                snprintf(cmd, sizeof(cmd),
                         "cp -r \"%s\" \"%s/%s\" 2>/dev/null",
                         extra_srcs[i], dst_parent, dirname);
                system(cmd);
            }
        }
    }

    fprintf(stderr,
        "[keybag] Data protection fix applied (%d keybag file(s) copied)\n"
        "  NOTE: Both OSes will now share the same passcode.\n",
        copied);
    return 0;
}

/* ── Verify keybag on secondary ──────────────────────────────── */
int keybag_verify_secondary(const char *secondary_mnt) {
    char path[512];
    struct stat st;

    snprintf(path, sizeof(path),
             "%s/private/var/keybags/systembag.kb", secondary_mnt);
    if (stat(path, &st) != 0) {
        fprintf(stderr, "[keybag] systembag.kb missing on secondary\n");
        return -1;
    }

    /* Compare size with primary — rough sanity check */
    struct stat st_primary;
    if (stat(KEYBAG_SYSTEM, &st_primary) == 0) {
        if (st.st_size != st_primary.st_size) {
            fprintf(stderr,
                "[keybag] WARNING: keybag size mismatch "
                "(primary=%lld, secondary=%lld)\n",
                (long long)st_primary.st_size, (long long)st.st_size);
        } else {
            fprintf(stderr, "[keybag] ✓ keybag verified on secondary\n");
        }
    }
    return 0;
}

/* ── Effaceable Storage read (advanced) ─────────────────────── */
/*
 * For a full Effaceable Storage mirror, we'd read specific LBAs
 * from /dev/rdisk0. This is needed only when the secondary OS
 * has a DIFFERENT keybag baked in (e.g. a factory restore IPSW).
 *
 * The Effaceable Storage on iOS is located at:
 *   A4 (s5l8930x): last ~128KB of NAND
 *   A5/A5X:        reserved region, accessible via IOFlashController
 *
 * For CoolBooter's use case (both OSes on same NAND, no erase),
 * the keybag file copy above is sufficient.
 * This function is provided for completeness / future use.
 *
 * Returns 0 on success, sets *out_buf and *out_size (caller must free).
 */
int keybag_read_effaceable(uint8_t **out_buf, size_t *out_size) {
    /* Platform-specific offsets */
    /* For A5 (iPhone 4S, iPad 2): read 1MB from end of /dev/rdisk0 */
    FILE *f = fopen("/dev/rdisk0", "rb");
    if (!f) {
        fprintf(stderr, "[keybag] Cannot open /dev/rdisk0 — "
                        "root permissions required\n");
        return -1;
    }

    /* Seek to the Effaceable Storage region */
    /* NOTE: Exact offset is device-specific and not publicly documented */
    /* This is a placeholder — real offset must be determined per-chip */
    fseek(f, -1 * 1024 * 1024, SEEK_END);  /* last 1MB as approximation */

    *out_size = 1024 * 1024;
    *out_buf = (uint8_t *)malloc(*out_size);
    if (!*out_buf) { fclose(f); return -1; }

    size_t r = fread(*out_buf, 1, *out_size, f);
    fclose(f);

    if (r < *out_size) {
        fprintf(stderr, "[keybag] Short read from Effaceable Storage\n");
        free(*out_buf); *out_buf = NULL;
        return -1;
    }

    fprintf(stderr, "[keybag] Read %zu bytes from Effaceable Storage\n", r);
    return 0;
}

/* Suppress compiler warnings for unused function in this TU */
static inline void _keybag_suppress(void) {
    uint8_t *b = NULL; size_t s = 0;
    (void)keybag_read_effaceable(&b, &s);
    free(b);
}
