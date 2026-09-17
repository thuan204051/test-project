#ifndef PATCHER_H
#define PATCHER_H

/* ============================================================
 * patcher.h — iBSS/iBEC binary patcher
 *
 * Two classes of patches:
 *
 * 1. Signature check bypass
 *    iBSS/iBEC verify RSA signatures on every image they load.
 *    We patch the comparison that gates boot to always succeed.
 *    Pattern: find the branch-on-fail after the verify call,
 *             replace with NOP (or unconditional branch past fail).
 *
 * 2. Boot partition redirect
 *    The iBEC passes boot-args to XNU including rd=disk0s1s2.
 *    We change s2 → s3 so XNU mounts the secondary rootfs.
 *    Also patches any raw "/dev/disk0s1s2" strings.
 *
 * 3. Boot-args injection (optional)
 *    Append custom boot-args (e.g. "-v" for verbose).
 * ============================================================ */

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/* Thumb2 NOP: 0x00BF (16-bit) */
#define THUMB_NOP_HI  0x00u
#define THUMB_NOP_LO  0xBFu

/* Thumb2 MOV R0, #0 + BX LR — "always return 0" trampoline */
/* MOVS R0, #0 = 0x2000; BX LR = 0x4770 */
#define THUMB_MOVS_R0_0_HI  0x20u
#define THUMB_MOVS_R0_0_LO  0x00u
#define THUMB_BX_LR_HI      0x47u
#define THUMB_BX_LR_LO      0x70u

/* Result of a patch operation */
typedef struct {
    int sig_bypass_count;     /* how many sig-check patches applied */
    int part_redirect_count;  /* how many partition redirects applied */
    int bootargs_patched;     /* 1 if boot-args were patched */
} patch_result_t;

/* Apply all standard dual-boot patches to a decrypted iBSS/iBEC buffer.
 *
 *   buf       : decrypted image buffer (modified IN PLACE)
 *   len       : buffer length
 *   is_ibec   : 1 if patching iBEC (enables boot-args/partition patches)
 *               0 if patching iBSS (only sig bypass)
 *   verbose   : append "-v" to boot-args
 *   result    : filled with counts of applied patches
 *
 * Returns 0 on success, -1 if critical patches could not be found. */
int patch_bootloader(uint8_t *buf, size_t len, int is_ibec,
                     int verbose, patch_result_t *result);

/* Bypass RSA signature checks.
 * Scans for the compare-and-branch pattern after verify calls.
 * Returns number of patches applied (0 = pattern not found). */
int patch_sig_checks(uint8_t *buf, size_t len);

/* Redirect root partition: replaces "disk0s1s2" → "disk0s1s3"
 * and "rd=disk0s1s2" in boot-args.
 * Returns number of occurrences patched. */
int patch_partition_redirect(uint8_t *buf, size_t len);

/* Append "-v" verbose flag to boot-args string in image.
 * Returns 1 if found and patched, 0 otherwise. */
int patch_verbose_boot(uint8_t *buf, size_t len);

/* Find byte pattern in buffer, return offset or -1 */
ssize_t mem_find(const uint8_t *haystack, size_t hay_len,
                 const uint8_t *needle, size_t needle_len);

/* Find string in buffer, return offset or -1 */
ssize_t mem_find_str(const uint8_t *buf, size_t len, const char *str);

#endif /* PATCHER_H */
