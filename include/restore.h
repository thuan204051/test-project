#ifndef RESTORE_H
#define RESTORE_H

/* ============================================================
 * restore.h — Restore secondary OS + kloader boot trigger
 *
 * Restore pipeline:
 *   1. ASR (Apple Software Restore) the rootfs DMG to /dev/disk0s1s3
 *   2. Mount secondary rootfs
 *   3. Activate (copy activation records)
 *   4. Optionally jailbreak secondary OS
 *   5. Copy patched iBSS/iBEC to work dir for kloader
 *   6. Unmount secondary
 *
 * Boot trigger (kloader):
 *   - Write patched iBSS/iBEC to well-known temp path
 *   - Exec multi_kloader with those paths
 *   - multi_kloader writes images into kernel memory (tfp0/hgsp4)
 *   - Triggers platform reset → device boots iBSS → iBEC → secondary iOS
 * ============================================================ */

typedef struct {
    char rootfs_dmg[512];      /* path to rootfs DMG */
    char target_dev[64];       /* e.g. /dev/disk0s1s3 */
    char mount_point[256];     /* temp mountpoint */
    int  copy_activation;      /* 1 = copy host activation records */
    int  jailbreak;            /* 1 = install jailbreak on secondary */
    char jailbreak_tool[256];  /* path to jailbreak patcher/tool */
    int  data_protection_fix;  /* 1 = iOS 9+ data protection workaround */
} cb_restore_config_t;

typedef struct {
    char ibss_path[512];       /* patched iBSS path */
    char ibec_path[512];       /* patched iBEC path */
    char kloader_path[256];    /* path to multi_kloader binary */
} cb_boot_config_t;

/* Restore rootfs DMG to secondary partition using ASR.
 * Returns 0 on success. */
int restore_rootfs(const cb_restore_config_t *cfg);

/* Mount the secondary system partition at cfg->mount_point.
 * Returns 0 on success. */
int restore_mount_secondary(cb_restore_config_t *cfg);

/* Unmount the secondary system partition.
 * Returns 0 on success. */
int restore_unmount_secondary(const cb_restore_config_t *cfg);

/* Copy host activation records to secondary OS.
 * Returns 0 on success. */
int restore_copy_activation(const cb_restore_config_t *cfg);

/* Apply data protection workaround for iOS 9+ hosts.
 * Copies Effaceable Storage keys to secondary OS keybag.
 * Returns 0 on success. */
int restore_data_protection_fix(const cb_restore_config_t *cfg);

/* Install a jailbreak onto the secondary OS rootfs.
 * Returns 0 on success. */
int restore_jailbreak_secondary(const cb_restore_config_t *cfg);

/* Store patched bootloaders and trigger kloader boot.
 * Returns 0 on success (device will reboot into secondary OS). */
int restore_boot_secondary(const cb_boot_config_t *cfg);

/* Check if multi_kloader is available and functional.
 * Returns 0 if ready, -1 if not found or not usable. */
int restore_check_kloader(const char *kloader_path);

#endif /* RESTORE_H */
