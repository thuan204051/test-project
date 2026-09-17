/* POSIX */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
/* ============================================================
 * restore.c — ASR restore + kloader boot trigger
 * ============================================================ */

#include "restore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#define CB_WORK_DIR      "/var/mobile/Media/CoolBooter"
#define CB_BOOT_IBSS     CB_WORK_DIR "/iBSS.patched"
#define CB_BOOT_IBEC     CB_WORK_DIR "/iBEC.patched"
#define KLOADER_PATH_DEF "/usr/local/bin/multi_kloader"
#define KLOADER_ALT      "/usr/bin/kloader"

/* Activation records source (on primary OS) */
#define ACTIVATION_SRC  "/var/root/Library/Lockdown"
#define ACTIVATION_DST  "var/root/Library/Lockdown"  /* relative to mnt */

/* ── Helper ─────────────────────────────────────────────────── */
static int run_log(const char *cmd) {
    fprintf(stderr, "[restore] exec: %s\n", cmd);
    int r = system(cmd);
    if (r != 0) fprintf(stderr, "[restore] failed: exit %d\n", r);
    return (r == 0) ? 0 : -1;
}

/* ── ASR Restore ─────────────────────────────────────────────── */
/*
 * ASR (Apple Software Restore) is a Darwin tool that block-copies
 * a source DMG to a target disk partition with verification.
 *
 * Command: asr restore --source <dmg> --target <dev>
 *          --noprompt --noverify --erase
 *
 * On iOS, asr is available at /usr/sbin/asr.
 * The --erase flag formats the target before restoring.
 * After restore, the partition label will match the DMG volume name.
 */
int restore_rootfs(const cb_restore_config_t *cfg) {
    if (!cfg) return -1;

    fprintf(stderr, "[restore] ASR restore starting...\n");
    fprintf(stderr, "  source: %s\n", cfg->rootfs_dmg);
    fprintf(stderr, "  target: %s\n", cfg->target_dev);
    fprintf(stderr, "  ⚠ This will ERASE %s\n", cfg->target_dev);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "asr restore"
             " --source \"%s\""
             " --target %s"
             " --noprompt"
             " --noverify"
             " --erase"
             " 2>&1",
             cfg->rootfs_dmg, cfg->target_dev);

    int ret = run_log(cmd);
    if (ret != 0) {
        fprintf(stderr, "[restore] ASR failed — possible causes:\n"
                        "  - Not enough space on %s\n"
                        "  - Partition not properly formatted\n"
                        "  - DMG file corrupted\n",
                cfg->target_dev);
        return -1;
    }
    fprintf(stderr, "[restore] ASR restore complete\n");
    return 0;
}

/* ── Mount / Unmount ─────────────────────────────────────────── */
int restore_mount_secondary(cb_restore_config_t *cfg) {
    if (!cfg) return -1;

    /* Choose / create mount point */
    if (cfg->mount_point[0] == '\0')
        snprintf(cfg->mount_point, sizeof(cfg->mount_point),
                 "%s/mnt", CB_WORK_DIR);
    mkdir(cfg->mount_point, 0755);

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "mount -t hfs %s \"%s\" 2>&1",
             cfg->target_dev, cfg->mount_point);
    int ret = run_log(cmd);
    if (ret != 0) {
        fprintf(stderr, "[restore] Mount failed — trying with -o rw\n");
        snprintf(cmd, sizeof(cmd),
                 "mount -t hfs -o rw %s \"%s\" 2>&1",
                 cfg->target_dev, cfg->mount_point);
        ret = run_log(cmd);
    }
    return ret;
}

int restore_unmount_secondary(const cb_restore_config_t *cfg) {
    if (!cfg || cfg->mount_point[0] == '\0') return -1;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "umount \"%s\" 2>&1", cfg->mount_point);
    return run_log(cmd);
}

/* ── Activation records ──────────────────────────────────────── */
int restore_copy_activation(const cb_restore_config_t *cfg) {
    if (!cfg || cfg->mount_point[0] == '\0') return -1;

    char dst[512];
    snprintf(dst, sizeof(dst), "%s/%s", cfg->mount_point, ACTIVATION_DST);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "mkdir -p \"%s\" && cp -r \"%s/\" \"%s/\" 2>&1",
             dst, ACTIVATION_SRC, dst);

    fprintf(stderr, "[restore] Copying activation records...\n");
    return run_log(cmd);
}

/* ── Data protection workaround (iOS 9+ host) ───────────────── */
/*
 * iOS 9 introduced enhanced data protection that ties app data keys
 * to the Effaceable Storage keybag for the booted partition.
 *
 * When booting a secondary partition, the keybag on disk doesn't match
 * what apps expect, causing decryption failures (apps won't open).
 *
 * Fix: Copy the Effaceable Storage contents from the primary OS to the
 * secondary OS. Both will then use the same passcode and keybag.
 *
 * ⚠ Side effect: both OSes share the same data protection keys,
 *   meaning the same passcode unlocks either OS.
 *
 * Effaceable Storage on iOS: accessed via /dev/rdisk0 at known offsets,
 * or via the IOKit IOFlashController interface.
 */
int restore_data_protection_fix(const cb_restore_config_t *cfg) {
    if (!cfg || cfg->mount_point[0] == '\0') return -1;

    fprintf(stderr, "[restore] Applying iOS 9+ data protection fix...\n");

    /* Source: primary OS keybag data */
    const char *keybag_src = "/private/var/keybags/systembag.kb";
    char keybag_dst[512];
    snprintf(keybag_dst, sizeof(keybag_dst),
             "%s/private/var/keybags/", cfg->mount_point);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "mkdir -p \"%s\" && cp \"%s\" \"%s\" 2>&1",
             keybag_dst, keybag_src, keybag_dst);
    run_log(cmd);

    /* Also copy the emf keybag if it exists */
    const char *emf_src = "/private/var/keybags/emfkeybag.plist";
    struct stat st;
    if (stat(emf_src, &st) == 0) {
        snprintf(cmd, sizeof(cmd),
                 "cp \"%s\" \"%s\" 2>&1", emf_src, keybag_dst);
        run_log(cmd);
    }

    fprintf(stderr, "[restore] Data protection fix applied\n");
    return 0;
}

/* ── Jailbreak secondary ─────────────────────────────────────── */
/*
 * For iOS 8+ as secondary OS, applying a jailbreak is required
 * before first boot (otherwise SpringBoard crashes on first launch).
 *
 * The jailbreak tool patches:
 *   - /private/var/db/launchd.db/com.apple.launchd/overrides.plist
 *   - Installs Cydia.app + bootstrap
 *   - Patches amfid (signature verification daemon)
 *
 * For CoolBooter, the jailbreak_tool is typically the CLI version
 * of evasi0n/Pangu/TaiG/etc. packed into the IPSW payload or
 * bundled with CoolBooter.
 *
 * This implementation calls an external patcher with the secondary
 * rootfs mountpoint as argument.
 */
int restore_jailbreak_secondary(const cb_restore_config_t *cfg) {
    if (!cfg) return -1;

    struct stat st;
    if (stat(cfg->jailbreak_tool, &st) != 0) {
        fprintf(stderr, "[restore] Jailbreak tool not found: %s\n",
                cfg->jailbreak_tool);
        return -1;
    }

    fprintf(stderr, "[restore] Applying jailbreak to secondary OS...\n");
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "\"%s\" \"%s\" 2>&1",
             cfg->jailbreak_tool, cfg->mount_point);
    return run_log(cmd);
}

/* ── kloader check ───────────────────────────────────────────── */
int restore_check_kloader(const char *kloader_path) {
    const char *path = kloader_path ? kloader_path : KLOADER_PATH_DEF;
    struct stat st;
    if (stat(path, &st) == 0) {
        /* Check executable */
        if (st.st_mode & S_IXUSR) {
            fprintf(stderr, "[restore] kloader found: %s\n", path);
            return 0;
        }
    }
    /* Try alternate */
    if (stat(KLOADER_ALT, &st) == 0 && (st.st_mode & S_IXUSR)) {
        fprintf(stderr, "[restore] kloader found (alt): %s\n", KLOADER_ALT);
        return 0;
    }
    fprintf(stderr, "[restore] kloader not found — cannot trigger boot\n"
                    "  Install multi_kloader from: "
                    "https://github.com/jonathanseals/ios-kexec-utils\n");
    return -1;
}

/* ── Boot trigger ─────────────────────────────────────────────── */
/*
 * kloader / multi_kloader boot flow:
 *
 *   multi_kloader <ibss_path> <ibec_path>
 *
 * Internally, multi_kloader:
 *   1. Opens /dev/kmem or uses task_for_pid(0) / hgsp4
 *   2. Reads the IOKit IOPlatformExpertDevice to get chip info
 *   3. Loads iBSS image into kernel memory at the correct load address
 *   4. Loads iBEC image into kernel memory after iBSS
 *   5. Triggers a platform reset (syscall or IOKit panic path)
 *   6. Device resets → BootROM loads iBSS from SRAM
 *   7. iBSS loads iBEC from NAND
 *   8. iBEC loads kernelcache from /dev/disk0s1s3 (patched)
 *   9. XNU boots with rd=disk0s1s3 boot-arg
 *
 * After this call returns (if it does), the device is rebooting.
 * It does NOT return in the normal sense — the device resets.
 */
int restore_boot_secondary(const cb_boot_config_t *cfg) {
    if (!cfg) return -1;

    /* Resolve kloader binary */
    const char *kloader = cfg->kloader_path[0] ? cfg->kloader_path
                                                : KLOADER_PATH_DEF;
    struct stat st;
    if (stat(kloader, &st) != 0) {
        kloader = KLOADER_ALT;
        if (stat(kloader, &st) != 0) {
            fprintf(stderr, "[restore] kloader not found\n");
            return -1;
        }
    }

    /* Copy patched bootloaders to expected paths */
    char cmd[2048];
    if (strcmp(cfg->ibss_path, CB_BOOT_IBSS) != 0) {
        snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s\" && chmod 644 \"%s\"",
                 cfg->ibss_path, CB_BOOT_IBSS, CB_BOOT_IBSS);
        run_log(cmd);
    }
    if (strcmp(cfg->ibec_path, CB_BOOT_IBEC) != 0) {
        snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s\" && chmod 644 \"%s\"",
                 cfg->ibec_path, CB_BOOT_IBEC, CB_BOOT_IBEC);
        run_log(cmd);
    }

    fprintf(stderr, "[restore] Triggering kloader boot...\n");
    fprintf(stderr, "  iBSS: %s\n", CB_BOOT_IBSS);
    fprintf(stderr, "  iBEC: %s\n", CB_BOOT_IBEC);
    fprintf(stderr, "  kloader: %s\n", kloader);
    fprintf(stderr, "  ⚠ Device will reboot NOW into secondary OS\n");
    fflush(stderr);
    fflush(stdout);
    sleep(1);  /* give user a moment to read */

    /* Launch kloader — this triggers platform reset.
     * Device will not return from this call in normal operation. */
    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" \"%s\"",
             kloader, CB_BOOT_IBSS, CB_BOOT_IBEC);
    int ret = system(cmd);  /* typically never returns */

    /* If we get here, kloader failed */
    fprintf(stderr, "[restore] kloader exited with %d — boot failed\n", ret);
    fprintf(stderr, "  Possible causes:\n"
                    "  - No tfp0 or hgsp4 (jailbreak required)\n"
                    "  - Wrong iBSS/iBEC for this device\n"
                    "  - kloader version mismatch\n");
    return -1;
}
