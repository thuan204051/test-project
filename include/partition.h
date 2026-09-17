#ifndef PARTITION_H
#define PARTITION_H

/* ============================================================
 * partition.h — NAND partition management
 *
 * iOS uses LwVM (Lightweight Volume Manager) on top of the raw NAND.
 * Partition layout before CoolBooter:
 *   /dev/disk0        raw NAND
 *   /dev/disk0s1      LwVM logical volume container
 *   /dev/disk0s1s1    System (root FS, read-only, HFS+)
 *   /dev/disk0s1s2    Data (/var, read-write, HFS+)
 *
 * After CoolBooter setup:
 *   /dev/disk0s1s1    System primary   (shrunk)
 *   /dev/disk0s1s2    Data primary     (shrunk)
 *   /dev/disk0s1s3    System secondary (new)
 *   /dev/disk0s1s4    Data secondary   (new, optional)
 *
 * The partition resizing is done via gptfdisk (gdisk fork adapted
 * for LwVM) which CoolBooter shipped as a compiled binary.
 * This reimplementation calls the system gdisk/gptfdisk tool.
 *
 * ⚠ WARNING: Partition operations on live NAND are DANGEROUS.
 *   Data loss is possible if interrupted.
 * ============================================================ */

#include <stddef.h>

#define CB_MAIN_DISK      "/dev/disk0"
#define CB_LVM_DISK       "/dev/disk0s1"
#define CB_PRIMARY_SYS    "/dev/disk0s1s1"   /* main iOS rootfs */
#define CB_PRIMARY_DATA   "/dev/disk0s1s2"   /* main /var        */
#define CB_SECONDARY_SYS  "/dev/disk0s1s3"   /* dual-boot rootfs */
#define CB_SECONDARY_DATA "/dev/disk0s1s4"   /* dual-boot /var   */

/* Partition info */
typedef struct {
    unsigned long long size_bytes;    /* current size */
    unsigned long long free_bytes;    /* free space */
    char               fstype[32];    /* e.g. "HFS+" */
    int                mounted;       /* 1 if currently mounted */
    char               mountpoint[128];
} cb_partition_info_t;

/* Configuration for secondary partition creation */
typedef struct {
    unsigned long long secondary_sys_mb;   /* secondary rootfs size in MiB */
    unsigned long long secondary_data_mb;  /* secondary data size in MiB (0=no separate data) */
    int                keep_backups;       /* 1 = save partition table backup */
} cb_partition_config_t;

/* Query partition info for a device node.
 * Returns 0 on success. */
int partition_info(const char *dev, cb_partition_info_t *out);

/* Get total free space available for secondary partition (in bytes).
 * Considers minimum required size for primary partitions. */
unsigned long long partition_available_bytes(void);

/* Create the secondary partitions.
 * This is the point-of-no-return for partition layout changes.
 * Backs up current partition table to CB_WORK_DIR/backup_gpt.bin.
 * Returns 0 on success. */
int partition_create_secondary(const cb_partition_config_t *cfg);

/* Remove secondary partitions and restore original sizes.
 * Returns 0 on success. */
int partition_remove_secondary(void);

/* Format the secondary system partition as HFS+.
 * Returns 0 on success. */
int partition_format_secondary_sys(void);

/* Check if secondary partition already exists.
 * Returns 1 if disk0s1s3 exists, 0 otherwise. */
int partition_secondary_exists(void);

/* Backup GPT/LwVM partition table to file.
 * Returns 0 on success. */
int partition_backup(const char *out_path);

/* Restore GPT/LwVM partition table from backup.
 * Returns 0 on success. */
int partition_restore_backup(const char *backup_path);

#endif /* PARTITION_H */
