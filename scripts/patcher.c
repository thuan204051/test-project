/* ============================================================
 * patcher.c — iBSS/iBEC binary patcher implementation
 * ============================================================ */

#include "patcher.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Memory search ─────────────────────────────────────────── */
ssize_t mem_find(const uint8_t *hay, size_t hay_len,
                 const uint8_t *needle, size_t needle_len) {
    if (needle_len == 0 || needle_len > hay_len) return -1;
    for (size_t i = 0; i <= hay_len - needle_len; i++)
        if (memcmp(hay + i, needle, needle_len) == 0)
            return (ssize_t)i;
    return -1;
}

ssize_t mem_find_str(const uint8_t *buf, size_t len, const char *str) {
    return mem_find(buf, len, (const uint8_t *)str, strlen(str));
}

/* ── Signature check bypass ────────────────────────────────── */
/*
 * The iBSS/iBEC verify function returns 0 on success.
 * After calling it, the caller checks:
 *   CMP R0, #0
 *   BNE  <error_handler>
 *
 * In Thumb2 encoding:
 *   CMP R0, #0  =>  00 28         (THUMB 16-bit: CMP Rn, #imm8)
 *   BNE offset  =>  XX D1         (THUMB 16-bit: BNE label)
 *                or F0 80 XX XX   (THUMB 32-bit wide BNE)
 *
 * Strategy: replace BNE with NOP (or change to BEQ which never fires
 * on non-zero, effectively ignoring the fail path).
 *
 * We also handle the pattern where the entire verify function is
 * replaced with MOV R0,#0 ; BX LR.
 */

/* Pattern 1: CMP R0,#0 + BNE (16-bit) */
static const uint8_t PAT_CMP_BNE_16[] = { 0x00, 0x28 };    /* CMP R0, #0 */
/* The BNE immediately follows: 0xXX 0xD1 */

/* Pattern 2: CBNZ R0, label (Thumb2 "compare and branch if non-zero") */
/* CBNZ Rn, label: 0xX8 0xB9 (16-bit) */
static const uint8_t PAT_CBNZ_PREFIX[] = { 0xB9 };

/* Pattern 3: known Thumb2 sequence for verify_shsh result check */
/* BL verify_shsh: 4-byte (F7 FF BL offset / FF F7 BL offset) then CMP */

int patch_sig_checks(uint8_t *buf, size_t len) {
    int count = 0;

    for (size_t i = 0; i + 4 < len; i++) {
        /* Pattern: CMP R0, #0 (0x2800) followed immediately by BNE (0xD1XX) */
        if (buf[i] == 0x00 && buf[i+1] == 0x28) {       /* CMP R0, #0 */
            if (buf[i+2+1] == 0xD1) {                    /* BNE Rn (16-bit) */
                fprintf(stderr, "[patch] sig bypass at 0x%zx: "
                                "CMP+BNE(16) -> NOP+NOP\n", i);
                buf[i+2] = THUMB_NOP_LO;  /* NOP (low) */
                buf[i+3] = THUMB_NOP_HI;  /* NOP (high) = 0xBF00 */
                count++;
                i += 3;
                continue;
            }
            /* Wide BNE: 40 F0 XX XX or F0 80 XX XX */
            if (i + 6 < len &&
                buf[i+2] == 0x40 && buf[i+3] == 0xF0) {
                fprintf(stderr, "[patch] sig bypass at 0x%zx: "
                                "CMP+BNE(32) -> NOPs\n", i);
                /* NOP + NOP (each 16-bit: 0xBF00) */
                buf[i+2] = 0x00; buf[i+3] = 0xBF;
                buf[i+4] = 0x00; buf[i+5] = 0xBF;
                count++;
                i += 5;
                continue;
            }
        }

        /* Pattern: CBNZ R0/R1, label  (B9 XX) */
        /* Replace CBNZ with NOP NOP */
        if (i + 2 < len && buf[i+1] == 0xB9) {
            uint8_t rn = buf[i] & 0x07;
            if (rn <= 1) {  /* only patch for R0 or R1 results */
                fprintf(stderr, "[patch] sig bypass at 0x%zx: CBNZ R%u -> NOP\n",
                        i, rn);
                buf[i]   = THUMB_NOP_LO;
                buf[i+1] = THUMB_NOP_HI;
                count++;
                i++;
            }
        }

        /* Pattern: TST R0, R0 (0x00 0x42) followed by BNE */
        if (i + 4 < len && buf[i] == 0x00 && buf[i+1] == 0x42) {
            if (buf[i+3] == 0xD1) {
                fprintf(stderr, "[patch] sig bypass at 0x%zx: TST+BNE -> NOPs\n", i);
                buf[i+2] = THUMB_NOP_LO; buf[i+3] = THUMB_NOP_HI;
                count++;
                i += 3;
            }
        }
    }

    fprintf(stderr, "[patch] sig_bypass: %d patches applied\n", count);
    return count;
}

/* ── Partition redirect ─────────────────────────────────────── */
/*
 * The iBEC encodes the root device in multiple ways:
 *   1. As a boot-arg string: "rd=disk0s1s2"
 *   2. As a device path: "/dev/disk0s1s2" or "disk0s1s2"
 *   3. As a component in other strings
 *
 * We replace every occurrence of "s1s2\0" or "s1s2 " or "s1s2\n"
 * that appears after "disk0" with "s1s3".
 */
int patch_partition_redirect(uint8_t *buf, size_t len) {
    int count = 0;
    const char *patterns[] = {
        "disk0s1s2",
        "rd=disk0s1s2",
        NULL
    };

    for (int p = 0; patterns[p]; p++) {
        size_t plen = strlen(patterns[p]);
        uint8_t *search = buf;
        size_t remain = len;
        while (remain >= plen) {
            ssize_t off = mem_find(search, remain,
                                   (const uint8_t *)patterns[p], plen);
            if (off < 0) break;
            /* Replace the trailing '2' with '3' */
            search[off + plen - 1] = '3';
            fprintf(stderr, "[patch] partition redirect at 0x%zx: %s -> ...s3\n",
                    (size_t)(search - buf) + off, patterns[p]);
            count++;
            search += off + plen;
            remain -= off + plen;
        }
    }

    fprintf(stderr, "[patch] partition_redirect: %d patches applied\n", count);
    return count;
}

/* ── Verbose boot ───────────────────────────────────────────── */
int patch_verbose_boot(uint8_t *buf, size_t len) {
    /* Find "rd=disk0s1s" (after our partition patch, it's "s3") */
    /* Boot args in iBEC look like: "rd=disk0s1s2 -progress" */
    const char *boot_args_marker = "rd=disk0s1s";
    ssize_t off = mem_find_str(buf, len, boot_args_marker);
    if (off < 0) {
        fprintf(stderr, "[patch] verbose: boot-args marker not found\n");
        return 0;
    }

    /* Scan forward to end of string (null terminator or max 256 bytes) */
    uint8_t *ba = buf + off;
    size_t ba_max = (len - off < 256) ? len - off : 256;
    size_t ba_len = 0;
    while (ba_len < ba_max && ba[ba_len] != '\0') ba_len++;

    if (ba_len + 3 >= ba_max) {
        fprintf(stderr, "[patch] verbose: no room to append -v\n");
        return 0;
    }

    /* Append " -v" to the boot-args string */
    ba[ba_len]   = ' ';
    ba[ba_len+1] = '-';
    ba[ba_len+2] = 'v';
    ba[ba_len+3] = '\0';
    fprintf(stderr, "[patch] verbose: appended -v to boot-args\n");
    return 1;
}

/* ── Master patch function ──────────────────────────────────── */
int patch_bootloader(uint8_t *buf, size_t len, int is_ibec,
                     int verbose, patch_result_t *result) {
    if (!buf || len == 0) return -1;
    if (result) memset(result, 0, sizeof(*result));

    int rc = 0;

    /* Step 1: Bypass signature checks (both iBSS and iBEC) */
    int sig_count = patch_sig_checks(buf, len);
    if (result) result->sig_bypass_count = sig_count;
    if (sig_count == 0) {
        fprintf(stderr, "[patch] WARNING: no sig check patterns found "
                        "— image may be wrong version or already patched\n");
        rc = -1;
    }

    if (is_ibec) {
        /* Step 2: Redirect boot partition s2 → s3 */
        int part_count = patch_partition_redirect(buf, len);
        if (result) result->part_redirect_count = part_count;
        if (part_count == 0) {
            fprintf(stderr, "[patch] WARNING: partition pattern not found "
                            "in iBEC — dual boot may fail\n");
            rc = -1;
        }

        /* Step 3: Verbose boot (optional) */
        if (verbose) {
            int vb = patch_verbose_boot(buf, len);
            if (result) result->bootargs_patched = vb;
        }
    }

    return rc;
}
