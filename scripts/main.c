/* POSIX */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
/* ============================================================
 * main.c — CoolBooter CLI (Open Source Reimplementation)
 *
 * Clean-room reimplementation of coolbootercli based on
 * documented behavior, public tool interfaces, and the
 * open-source components CoolBooter depended on.
 *
 * Build: see Makefile (requires Theos or iOS SDK cross-compile)
 * Target: jailbroken 32-bit iOS (armv7, iOS 5–10.3.4)
 *
 * ⚠ RISK NOTICE:
 *   This tool modifies the device's NAND partition table.
 *   Data loss is possible. Always backup important data first.
 *   Use only on a device you can afford to restore if needed.
 *
 * License: MIT
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/sysctl.h>

#include "devices.h"
#include "img3.h"
#include "ipsw.h"
#include "patcher.h"
#include "partition.h"
#include "restore.h"

#define CB_VERSION        "1.0-oss"
#define CB_WORK_DIR       "/var/mobile/Media/CoolBooter"
#define CB_KLOADER_PATH   "/usr/local/bin/multi_kloader"
#define CB_MIN_SPACE_MB   1024ULL  /* minimum 1GB for secondary */

/* ── Argument state ─────────────────────────────────────────── */
typedef struct {
    char  target_version[32];  /* -t: iOS version to install  */
    char  ipsw_path[512];      /* -i: local IPSW file         */
    char  ibss_key[65];        /* --ibss-key: explicit key    */
    char  ibss_iv[33];         /* --ibss-iv                   */
    char  ibec_key[65];        /* --ibec-key                  */
    char  ibec_iv[33];         /* --ibec-iv                   */
    int   secondary_size_mb;   /* -s: partition size          */
    int   jailbreak;           /* -j: jailbreak secondary     */
    int   verbose_boot;        /* -v: verbose boot (-v flag)  */
    int   data_protection;     /* -d: data protection fix     */
    int   boot_only;           /* -b: just boot, no install   */
    int   remove;              /* -r: remove secondary        */
    int   force;               /* -f: skip confirmations      */
} cb_args_t;

/* ── Utilities ───────────────────────────────────────────────── */
static void die(const char *msg) {
    fprintf(stderr, "[coolbooter] FATAL: %s\n", msg);
    exit(1);
}

static void banner(void) {
    fprintf(stderr,
        "╔══════════════════════════════════════╗\n"
        "║  CoolBooter CLI (Open Source) v%-6s║\n"
        "║  32-bit iOS Dual Boot Utility        ║\n"
        "╚══════════════════════════════════════╝\n",
        CB_VERSION);
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n\n"
        "Install:\n"
        "  -t <version>    Target iOS version (e.g. 6.1.3)\n"
        "  -i <path>       Local IPSW file (skip download)\n"
        "  -s <mb>         Secondary system size in MB (default: auto)\n"
        "  -j              Jailbreak secondary OS (default: yes for iOS 8+)\n"
        "  -v              Enable verbose boot on secondary OS\n"
        "  -d              Data protection fix (needed if host is iOS 9+)\n"
        "  -f              Skip confirmation prompts\n\n"
        "Key overrides (if IPSW keys not in built-in DB):\n"
        "  --ibss-key <hex>  iBSS AES key\n"
        "  --ibss-iv  <hex>  iBSS AES IV\n"
        "  --ibec-key <hex>  iBEC AES key\n"
        "  --ibec-iv  <hex>  iBEC AES IV\n\n"
        "Operations:\n"
        "  -b              Boot into secondary OS (already installed)\n"
        "  -r              Remove secondary OS\n"
        "  -h              Show this help\n\n"
        "Examples:\n"
        "  %s -t 6.1.3 -j -v         Install iOS 6.1.3 with jailbreak\n"
        "  %s -i ~/iOS6.ipsw -t 6.1.3\n"
        "  %s -b                      Boot existing secondary OS\n"
        "  %s -r                      Remove secondary OS\n",
        prog, prog, prog, prog, prog);
}

/* ── Device detection ────────────────────────────────────────── */
static int detect_device(char *product, size_t product_sz,
                          char *hw_model, size_t hw_sz,
                          int *ios_major) {
    /* Get device product type via sysctl */
    size_t len = product_sz;
    if (sysctlbyname("hw.machine", product, &len, NULL, 0) != 0) {
        /* Fallback: read from /System/Library/CoreServices/SystemVersion.plist */
        FILE *fp = popen("sysctl -n hw.machine 2>/dev/null", "r");
        if (!fp) return -1;
        fgets(product, product_sz, fp);
        pclose(fp);
        size_t l = strlen(product);
        if (l > 0 && product[l-1] == '\n') product[l-1] = '\0';
    }

    /* Get hardware model */
    struct utsname u;
    uname(&u);
    strncpy(hw_model, u.machine, hw_sz - 1);

    /* Get iOS major version */
    char os_ver[64] = {0};
    len = sizeof(os_ver);
    sysctlbyname("kern.osrelease", os_ver, &len, NULL, 0);
    /* kern.osrelease is XNU version, not iOS version.
     * Read from SystemVersion.plist instead. */
    FILE *fp = popen("sw_vers -productVersion 2>/dev/null || "
                     "defaults read /System/Library/CoreServices/"
                     "SystemVersion ProductVersion 2>/dev/null", "r");
    if (fp) {
        char ver[32] = {0};
        fgets(ver, sizeof(ver), fp);
        pclose(fp);
        *ios_major = atoi(ver);
    }
    return 0;
}

/* ── Prerequisite checks ─────────────────────────────────────── */
static int check_prerequisites(const char *product, int ios_major) {
    int ok = 1;

    /* Check tfp0 or hgsp4 */
    fprintf(stderr, "[check] Testing kernel access (tfp0/hgsp4)...\n");
    /* Try to open /dev/kmem as a quick check */
    FILE *km = fopen("/dev/kmem", "r");
    if (!km) {
        /* Check for multi_kloader which self-tests */
        struct stat st;
        if (stat(CB_KLOADER_PATH, &st) == 0) {
            int ret = system(CB_KLOADER_PATH " --check 2>/dev/null");
            if (ret != 0) {
                fprintf(stderr,
                    "[check] ✗ No kernel access — jailbreak required\n"
                    "  Supported: Pangu, TaiG, 3uTools, Yalu, h3lix, Phoenix\n");
                ok = 0;
            } else {
                fprintf(stderr, "[check] ✓ Kernel access via kloader\n");
            }
        } else {
            fprintf(stderr, "[check] ✗ kloader not found at %s\n"
                            "  Install multi_kloader to continue\n",
                    CB_KLOADER_PATH);
            ok = 0;
        }
    } else {
        fclose(km);
        fprintf(stderr, "[check] ✓ Kernel access via /dev/kmem\n");
    }

    /* Check device support */
    const cb_device_t *dev = cb_find_device(product);
    if (!dev) {
        fprintf(stderr, "[check] ✗ Device not supported: %s\n", product);
        fprintf(stderr, "  CoolBooter supports 32-bit devices (A4–A6X chip)\n");
        ok = 0;
    } else {
        fprintf(stderr, "[check] ✓ Device: %s (%s)\n",
                dev->friendly, dev->chip);
    }

    /* Check available space */
    unsigned long long avail = partition_available_bytes();
    fprintf(stderr, "[check] Available NAND for secondary: %llu MB\n",
            avail / 1024 / 1024);
    if (avail < CB_MIN_SPACE_MB * 1024ULL * 1024ULL) {
        fprintf(stderr, "[check] ✗ Insufficient space (need %llu MB)\n",
                CB_MIN_SPACE_MB);
        ok = 0;
    } else {
        fprintf(stderr, "[check] ✓ Sufficient space\n");
    }

    return ok ? 0 : -1;
}

/* ── Confirmation prompt ─────────────────────────────────────── */
static int confirm(const char *msg) {
    fprintf(stderr, "\n⚠ WARNING: %s\n", msg);
    fprintf(stderr, "Type 'yes' to continue: ");
    char buf[16] = {0};
    fgets(buf, sizeof(buf), stdin);
    return (strncmp(buf, "yes", 3) == 0) ? 0 : -1;
}

/* ── Phase: Prepare firmware ─────────────────────────────────── */
static int phase_prepare_firmware(const cb_args_t *args,
                                   const char *product,
                                   ipsw_t *ipsw_out,
                                   char *ibss_dec, size_t ibss_sz,
                                   char *ibec_dec, size_t ibec_sz) {
    const cb_firmware_t *fw = cb_find_firmware(product, args->target_version);

    /* Step 1: Get IPSW */
    char ipsw_path[512] = {0};
    if (args->ipsw_path[0]) {
        strncpy(ipsw_path, args->ipsw_path, sizeof(ipsw_path) - 1);
        fprintf(stderr, "[firmware] Using local IPSW: %s\n", ipsw_path);
    } else {
        if (!fw || !fw->ipsw_url) {
            fprintf(stderr, "[firmware] No download URL for %s %s\n",
                    product, args->target_version);
            fprintf(stderr, "  Provide IPSW manually with -i, or add entry to devices.h\n");
            return -1;
        }

        if (ipsw_is_cached(product, args->target_version)) {
            ipsw_get_cached_path(product, args->target_version,
                                 ipsw_path, sizeof(ipsw_path));
            fprintf(stderr, "[firmware] Using cached IPSW: %s\n", ipsw_path);
        } else {
            mkdir(CB_WORK_DIR "/ipsw", 0755);
            ipsw_get_cached_path(product, args->target_version,
                                 ipsw_path, sizeof(ipsw_path));
            fprintf(stderr, "[firmware] Downloading iOS %s for %s...\n",
                    args->target_version, product);
            if (ipsw_download(fw->ipsw_url, ipsw_path) != 0) return -1;
        }
    }

    /* Step 2: Extract IPSW */
    char extract_dir[512];
    snprintf(extract_dir, sizeof(extract_dir),
             "%s/extracted", CB_WORK_DIR);
    if (ipsw_extract(ipsw_path, extract_dir, ipsw_out) != 0) return -1;

    /* Step 3: Decrypt iBSS */
    {
        const char *key = (args->ibss_key[0]) ? args->ibss_key
                         : (fw ? fw->ibss_key : NULL);
        const char *iv  = (args->ibss_iv[0])  ? args->ibss_iv
                         : (fw ? fw->ibss_iv  : NULL);

        if (!key || !iv) {
            fprintf(stderr,
                "[firmware] No iBSS key/IV — provide via --ibss-key / --ibss-iv\n"
                "  Keys for many firmware versions: "
                "https://www.theiphonewiki.com/wiki/Firmware_Keys\n");
            return -1;
        }

        fprintf(stderr, "[firmware] Decrypting iBSS...\n");
        uint8_t *raw = NULL; size_t raw_sz = 0;
        FILE *f = fopen(ipsw_out->ibss_path, "rb");
        if (!f) { fprintf(stderr, "[firmware] Cannot open iBSS\n"); return -1; }
        fseek(f, 0, SEEK_END); raw_sz = ftell(f); rewind(f);
        raw = malloc(raw_sz);
        fread(raw, 1, raw_sz, f); fclose(f);

        img3_t img;
        if (img3_open(&img, raw, raw_sz) != 0) { free(raw); return -1; }
        if (img3_decrypt_keys(&img, key, iv) != 0) {
            img3_free(&img); free(raw); return -1;
        }

        snprintf(ibss_dec, ibss_sz, "%s/iBSS.dec", CB_WORK_DIR);
        img3_write_decrypted(&img, ibss_dec);
        img3_free(&img); free(raw);
        fprintf(stderr, "[firmware] iBSS decrypted: %s\n", ibss_dec);
    }

    /* Step 4: Decrypt iBEC */
    {
        const char *key = (args->ibec_key[0]) ? args->ibec_key
                         : (fw ? fw->ibec_key : NULL);
        const char *iv  = (args->ibec_iv[0])  ? args->ibec_iv
                         : (fw ? fw->ibec_iv  : NULL);

        if (!key || !iv) {
            fprintf(stderr, "[firmware] No iBEC key/IV — see iBSS message above\n");
            return -1;
        }

        fprintf(stderr, "[firmware] Decrypting iBEC...\n");
        uint8_t *raw = NULL; size_t raw_sz = 0;
        FILE *f = fopen(ipsw_out->ibec_path, "rb");
        if (!f) { fprintf(stderr, "[firmware] Cannot open iBEC\n"); return -1; }
        fseek(f, 0, SEEK_END); raw_sz = ftell(f); rewind(f);
        raw = malloc(raw_sz);
        fread(raw, 1, raw_sz, f); fclose(f);

        img3_t img;
        if (img3_open(&img, raw, raw_sz) != 0) { free(raw); return -1; }
        if (img3_decrypt_keys(&img, key, iv) != 0) {
            img3_free(&img); free(raw); return -1;
        }

        snprintf(ibec_dec, ibec_sz, "%s/iBEC.dec", CB_WORK_DIR);
        img3_write_decrypted(&img, ibec_dec);
        img3_free(&img); free(raw);
        fprintf(stderr, "[firmware] iBEC decrypted: %s\n", ibec_dec);
    }

    return 0;
}

/* ── Phase: Patch bootloaders ────────────────────────────────── */
static int phase_patch(const cb_args_t *args,
                       const char *ibss_dec, const char *ibec_dec,
                       char *ibss_pat, size_t ibss_psz,
                       char *ibec_pat, size_t ibec_psz) {
    /* Read, patch, write iBSS */
    {
        FILE *f = fopen(ibss_dec, "rb");
        if (!f) { fprintf(stderr, "[patch] Cannot open decrypted iBSS\n"); return -1; }
        fseek(f, 0, SEEK_END); size_t sz = ftell(f); rewind(f);
        uint8_t *buf = malloc(sz);
        fread(buf, 1, sz, f); fclose(f);

        patch_result_t r;
        int ret = patch_bootloader(buf, sz, 0 /*iBSS*/, 0, &r);
        fprintf(stderr, "[patch] iBSS: sig_bypass=%d\n", r.sig_bypass_count);
        if (ret != 0)
            fprintf(stderr, "[patch] WARNING: iBSS sig bypass may be incomplete\n");

        snprintf(ibss_pat, ibss_psz, "%s/iBSS.patched", CB_WORK_DIR);
        f = fopen(ibss_pat, "wb");
        if (!f) { free(buf); return -1; }
        fwrite(buf, 1, sz, f); fclose(f);
        free(buf);
    }

    /* Read, patch, write iBEC */
    {
        FILE *f = fopen(ibec_dec, "rb");
        if (!f) { fprintf(stderr, "[patch] Cannot open decrypted iBEC\n"); return -1; }
        fseek(f, 0, SEEK_END); size_t sz = ftell(f); rewind(f);
        uint8_t *buf = malloc(sz);
        fread(buf, 1, sz, f); fclose(f);

        patch_result_t r;
        int ret = patch_bootloader(buf, sz, 1 /*iBEC*/,
                                   args->verbose_boot, &r);
        fprintf(stderr, "[patch] iBEC: sig_bypass=%d partition_redirect=%d "
                        "verbose=%d\n",
                r.sig_bypass_count, r.part_redirect_count,
                r.bootargs_patched);
        if (ret != 0)
            fprintf(stderr, "[patch] WARNING: iBEC patch may be incomplete\n");

        snprintf(ibec_pat, ibec_psz, "%s/iBEC.patched", CB_WORK_DIR);
        f = fopen(ibec_pat, "wb");
        if (!f) { free(buf); return -1; }
        fwrite(buf, 1, sz, f); fclose(f);
        free(buf);
    }

    return 0;
}

/* ── Main ────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    banner();

    cb_args_t args;
    memset(&args, 0, sizeof(args));
    args.jailbreak        = -1;  /* -1 = auto */
    args.secondary_size_mb = 0;  /* 0 = auto  */

    /* ── Parse arguments ── */
    static struct option long_opts[] = {
        { "ibss-key",  required_argument, NULL, 1000 },
        { "ibss-iv",   required_argument, NULL, 1001 },
        { "ibec-key",  required_argument, NULL, 1002 },
        { "ibec-iv",   required_argument, NULL, 1003 },
        { "help",      no_argument,       NULL, 'h'  },
        { NULL, 0, NULL, 0 }
    };
    int opt, idx = 0;
    while ((opt = getopt_long(argc, argv, "t:i:s:jvdbrfh", long_opts, &idx)) != -1) {
        switch (opt) {
        case 't': strncpy(args.target_version, optarg, 31); break;
        case 'i': strncpy(args.ipsw_path, optarg, 511); break;
        case 's': args.secondary_size_mb = atoi(optarg); break;
        case 'j': args.jailbreak = 1; break;
        case 'v': args.verbose_boot = 1; break;
        case 'd': args.data_protection = 1; break;
        case 'b': args.boot_only = 1; break;
        case 'r': args.remove = 1; break;
        case 'f': args.force = 1; break;
        case 1000: strncpy(args.ibss_key, optarg, 64); break;
        case 1001: strncpy(args.ibss_iv,  optarg, 32); break;
        case 1002: strncpy(args.ibec_key, optarg, 64); break;
        case 1003: strncpy(args.ibec_iv,  optarg, 32); break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    /* ── Detect device ── */
    char product[64] = {0}, hw_model[64] = {0};
    int ios_major = 0;
    if (detect_device(product, sizeof(product),
                      hw_model, sizeof(hw_model), &ios_major) != 0)
        die("Could not detect device — are you on a jailbroken iOS device?");
    fprintf(stderr, "[info] Device: %s  iOS: %d.x\n", product, ios_major);

    /* Auto-enable jailbreak for iOS 8+ secondary */
    if (args.jailbreak == -1) {
        /* Parse target version major */
        int target_major = atoi(args.target_version);
        args.jailbreak = (target_major >= 8) ? 1 : 0;
        if (args.jailbreak)
            fprintf(stderr, "[info] Auto-enabling jailbreak for iOS 8+ target\n");
    }

    /* ── Operation: remove ── */
    if (args.remove) {
        if (!args.force && confirm("This will remove the secondary iOS installation") != 0) {
            fprintf(stderr, "Aborted.\n"); return 1;
        }
        return partition_remove_secondary();
    }

    /* ── Operation: boot only ── */
    if (args.boot_only) {
        if (!partition_secondary_exists()) {
            fprintf(stderr, "[boot] No secondary partition found\n"
                            "  Run without -b to install first\n");
            return 1;
        }
        cb_boot_config_t bcfg;
        memset(&bcfg, 0, sizeof(bcfg));
        strncpy(bcfg.ibss_path,   CB_WORK_DIR "/iBSS.patched",
                sizeof(bcfg.ibss_path) - 1);
        strncpy(bcfg.ibec_path,   CB_WORK_DIR "/iBEC.patched",
                sizeof(bcfg.ibec_path) - 1);
        strncpy(bcfg.kloader_path, CB_KLOADER_PATH,
                sizeof(bcfg.kloader_path) - 1);
        return restore_boot_secondary(&bcfg);
    }

    /* ── Operation: full install ── */
    if (args.target_version[0] == '\0') {
        fprintf(stderr, "[error] Target version required (-t)\n");
        usage(argv[0]); return 1;
    }

    /* Prerequisites */
    if (check_prerequisites(product, ios_major) != 0) {
        fprintf(stderr, "[error] Prerequisites not met — see above\n");
        return 1;
    }

    if (partition_secondary_exists()) {
        fprintf(stderr, "[error] Secondary partition already exists\n"
                        "  Use -r to remove first, or -b to just boot\n");
        return 1;
    }

    /* Confirmation */
    if (!args.force) {
        if (confirm("This will repartition the NAND and install a "
                    "secondary iOS. Device data may be at risk.") != 0) {
            fprintf(stderr, "Aborted.\n"); return 1;
        }
    }

    /* Phase 1: Firmware preparation */
    fprintf(stderr, "\n[Phase 1/5] Preparing firmware...\n");
    ipsw_t ipsw;
    char ibss_dec[512], ibec_dec[512];
    if (phase_prepare_firmware(&args, product, &ipsw,
                               ibss_dec, sizeof(ibss_dec),
                               ibec_dec, sizeof(ibec_dec)) != 0) {
        fprintf(stderr, "[error] Firmware preparation failed\n");
        return 1;
    }

    /* Phase 2: Patch bootloaders */
    fprintf(stderr, "\n[Phase 2/5] Patching bootloaders...\n");
    char ibss_pat[512], ibec_pat[512];
    if (phase_patch(&args, ibss_dec, ibec_dec,
                    ibss_pat, sizeof(ibss_pat),
                    ibec_pat, sizeof(ibec_pat)) != 0) {
        fprintf(stderr, "[error] Patching failed\n");
        return 1;
    }

    /* Phase 3: Partition NAND */
    fprintf(stderr, "\n[Phase 3/5] Partitioning NAND...\n");
    fprintf(stderr, "  ⚠ POINT OF NO RETURN — partitioning starts now\n");
    cb_partition_config_t pcfg;
    memset(&pcfg, 0, sizeof(pcfg));
    pcfg.secondary_sys_mb  = args.secondary_size_mb ? args.secondary_size_mb
                             : 2048;  /* 2GB default */
    pcfg.secondary_data_mb = 0;       /* share primary /var for now */
    pcfg.keep_backups      = 1;
    if (partition_create_secondary(&pcfg) != 0) {
        fprintf(stderr, "[error] Partitioning failed\n"
                        "  A backup is at %s/backup_gpt.bin\n", CB_WORK_DIR);
        return 1;
    }

    /* Phase 4: Restore rootfs */
    fprintf(stderr, "\n[Phase 4/5] Restoring secondary iOS...\n");
    cb_restore_config_t rcfg;
    memset(&rcfg, 0, sizeof(rcfg));
    strncpy(rcfg.rootfs_dmg,  ipsw.rootfs_path, sizeof(rcfg.rootfs_dmg) - 1);
    strncpy(rcfg.target_dev,  CB_SECONDARY_SYS,  sizeof(rcfg.target_dev) - 1);
    rcfg.copy_activation    = 1;
    rcfg.jailbreak          = args.jailbreak;
    rcfg.data_protection_fix = args.data_protection;

    if (restore_rootfs(&rcfg) != 0) {
        fprintf(stderr, "[error] Restore failed\n");
        return 1;
    }

    if (restore_mount_secondary(&rcfg) != 0) {
        fprintf(stderr, "[error] Cannot mount secondary — skipping post-install\n");
    } else {
        restore_copy_activation(&rcfg);
        if (rcfg.data_protection_fix)
            restore_data_protection_fix(&rcfg);
        if (rcfg.jailbreak && rcfg.jailbreak_tool[0])
            restore_jailbreak_secondary(&rcfg);
        restore_unmount_secondary(&rcfg);
    }

    /* Phase 5: Boot */
    fprintf(stderr, "\n[Phase 5/5] Booting secondary iOS...\n");
    cb_boot_config_t bcfg;
    memset(&bcfg, 0, sizeof(bcfg));
    strncpy(bcfg.ibss_path,    ibss_pat, sizeof(bcfg.ibss_path) - 1);
    strncpy(bcfg.ibec_path,    ibec_pat, sizeof(bcfg.ibec_path) - 1);
    strncpy(bcfg.kloader_path, CB_KLOADER_PATH, sizeof(bcfg.kloader_path) - 1);

    fprintf(stderr,
        "\n[success] Installation complete!\n"
        "  Secondary iOS %s installed on %s\n"
        "  Patched bootloaders: %s/iBSS.patched, iBEC.patched\n"
        "  To boot later: coolbootercli -b\n\n",
        args.target_version, CB_SECONDARY_SYS, CB_WORK_DIR);

    return restore_boot_secondary(&bcfg);
}
