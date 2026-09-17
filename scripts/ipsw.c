/* POSIX */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
/* ============================================================
 * ipsw.c — IPSW download and extraction
 * Uses: curl (download), unzip (extraction), CommonCrypto (SHA1)
 * ============================================================ */

#include "ipsw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

/* ── Download ───────────────────────────────────────────────── */
int ipsw_download(const char *url, const char *local_path) {
    if (!url || !local_path) return -1;

    /* Use system curl (available on jailbroken iOS via Cydia) */
    /* -L: follow redirects  -C -: resume  --progress-bar: human output */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "curl -L -C - --progress-bar -o \"%s\" \"%s\"",
             local_path, url);

    fprintf(stderr, "[ipsw] Downloading: %s\n", url);
    fprintf(stderr, "[ipsw] Destination: %s\n", local_path);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[ipsw] Download failed (curl exit %d)\n", ret);
        return -1;
    }

    /* Verify the file exists and has non-zero size */
    struct stat st;
    if (stat(local_path, &st) != 0 || st.st_size == 0) {
        fprintf(stderr, "[ipsw] Downloaded file missing or empty\n");
        return -1;
    }
    fprintf(stderr, "[ipsw] Download complete: %lld bytes\n",
            (long long)st.st_size);
    return 0;
}

/* ── Verification ───────────────────────────────────────────── */
int ipsw_verify(const char *path, const char *expected_sha1) {
    if (!path || !expected_sha1) return -1;

    char cmd[1024];
    char result[128] = {0};

    /* sha1sum is available on iOS jailbreaks (from Debian tools) */
    snprintf(cmd, sizeof(cmd), "sha1sum \"%s\" 2>/dev/null", path);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        /* Fallback: shasum */
        snprintf(cmd, sizeof(cmd), "shasum \"%s\" 2>/dev/null", path);
        fp = popen(cmd, "r");
        if (!fp) return -1;
    }
    fgets(result, sizeof(result), fp);
    pclose(fp);

    /* sha1sum output: "<hash>  <filename>" */
    result[40] = '\0'; /* truncate to hash only */
    if (strcasecmp(result, expected_sha1) != 0) {
        fprintf(stderr, "[ipsw] SHA1 mismatch:\n"
                        "  got:      %s\n"
                        "  expected: %s\n", result, expected_sha1);
        return -1;
    }
    fprintf(stderr, "[ipsw] SHA1 verified OK: %s\n", result);
    return 0;
}

/* ── Extract ────────────────────────────────────────────────── */
/*
 * IPSW is a ZIP. We use the system `unzip` to extract specific files.
 * Component names come from BuildManifest.plist.
 *
 * Since plist parsing is complex, we use a simpler heuristic:
 * scan the ZIP central directory for filenames matching known patterns.
 *
 * Known patterns (by chip):
 *   iBSS: Firmware/all_flash/iBSS.<variant>.RELEASE.dfu
 *   iBEC: Firmware/all_flash/iBEC.<variant>.RELEASE.dfu
 *   kernel: kernelcache.release.<board>
 *   rootfs: <build>.<variant>.dmg (largest .dmg file)
 */
static int find_ipsw_component(const char *ipsw_path, const char *pattern,
                               char *out_name, size_t out_sz) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "unzip -Z1 \"%s\" 2>/dev/null | grep -i \"%s\" | head -1",
             ipsw_path, pattern);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    int ok = (fgets(out_name, out_sz, fp) != NULL);
    pclose(fp);
    if (!ok) return -1;
    /* Strip trailing newline */
    size_t l = strlen(out_name);
    if (l > 0 && out_name[l-1] == '\n') out_name[l-1] = '\0';
    return 0;
}

static int extract_component(const char *ipsw_path, const char *component,
                              const char *dest_dir, char *out_path, size_t out_sz) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "unzip -o -j \"%s\" \"%s\" -d \"%s\" > /dev/null 2>&1",
             ipsw_path, component, dest_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "[ipsw] Failed to extract: %s\n", component);
        return -1;
    }
    /* out_path = dest_dir + "/" + basename(component) */
    const char *basename = strrchr(component, '/');
    basename = basename ? basename + 1 : component;
    snprintf(out_path, out_sz, "%s/%s", dest_dir, basename);
    fprintf(stderr, "[ipsw] Extracted: %s\n", out_path);
    return 0;
}

int ipsw_extract(const char *ipsw_path, const char *output_dir, ipsw_t *out) {
    if (!ipsw_path || !output_dir || !out) return -1;
    memset(out, 0, sizeof(*out));
    strncpy(out->path, ipsw_path, sizeof(out->path) - 1);

    /* Ensure output dir exists */
    mkdir(output_dir, 0755);

    char component[512];
    int errors = 0;

    /* iBSS */
    if (find_ipsw_component(ipsw_path, "iBSS.*RELEASE.dfu",
                            component, sizeof(component)) == 0) {
        extract_component(ipsw_path, component, output_dir,
                          out->ibss_path, sizeof(out->ibss_path));
    } else {
        fprintf(stderr, "[ipsw] WARNING: iBSS not found in IPSW\n");
        errors++;
    }

    /* iBEC */
    if (find_ipsw_component(ipsw_path, "iBEC.*RELEASE.dfu",
                            component, sizeof(component)) == 0) {
        extract_component(ipsw_path, component, output_dir,
                          out->ibec_path, sizeof(out->ibec_path));
    } else {
        fprintf(stderr, "[ipsw] WARNING: iBEC not found in IPSW\n");
        errors++;
    }

    /* kernelcache */
    if (find_ipsw_component(ipsw_path, "kernelcache.release",
                            component, sizeof(component)) == 0) {
        extract_component(ipsw_path, component, output_dir,
                          out->kernel_path, sizeof(out->kernel_path));
    } else {
        fprintf(stderr, "[ipsw] WARNING: kernelcache not found\n");
    }

    /* Root filesystem DMG (largest .dmg in IPSW) */
    {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "unzip -Z \"%s\" 2>/dev/null | "
                 "grep '\\.dmg$' | sort -k3 -n | tail -1 | "
                 "awk '{print $NF}'", ipsw_path);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            if (fgets(component, sizeof(component), fp)) {
                size_t l = strlen(component);
                if (l > 0 && component[l-1] == '\n') component[l-1] = '\0';
                extract_component(ipsw_path, component, output_dir,
                                  out->rootfs_path, sizeof(out->rootfs_path));
            }
            pclose(fp);
        }
    }

    if (errors > 0) {
        fprintf(stderr, "[ipsw] Extraction completed with %d errors\n", errors);
        return -1;
    }
    fprintf(stderr, "[ipsw] Extraction complete\n");
    return 0;
}

/* ── Cache management ───────────────────────────────────────── */
int ipsw_is_cached(const char *product, const char *version) {
    char path[512];
    ipsw_get_cached_path(product, version, path, sizeof(path));
    struct stat st;
    return (stat(path, &st) == 0 && st.st_size > 0) ? 1 : 0;
}

int ipsw_get_cached_path(const char *product, const char *version,
                         char *out, size_t out_sz) {
    if (!product || !version || !out) return -1;
    snprintf(out, out_sz, "%s/%s_%s_Restore.ipsw",
             CB_IPSW_CACHE_DIR, product, version);
    return 0;
}
