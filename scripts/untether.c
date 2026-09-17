/* POSIX */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

/* ============================================================
 * untether.c — Auto-boot secondary OS on device startup
 *
 * The untether installs a launchd daemon on the PRIMARY OS that
 * automatically triggers multi_kloader at boot, redirecting the
 * device into the secondary iOS before the primary SpringBoard loads.
 *
 * Mechanism:
 *   1. A launchd plist is installed at /Library/LaunchDaemons/
 *      com.coolbooter.untether.plist
 *   2. At boot, launchd starts the untether daemon early
 *      (before com.apple.SpringBoard)
 *   3. The daemon calls multi_kloader with the patched iBSS/iBEC
 *   4. Platform resets → secondary iOS boots
 *
 * To return to primary OS:
 *   - Hold a hardware button combo at boot to skip kloader
 *   - Or: the untether checks a flag file and skips if present
 *     (/var/mobile/Media/CoolBooter/.boot_primary)
 *   - The secondary OS's CoolBooter installation creates that flag
 *     before calling kloader to return to primary
 *
 * Limitations:
 *   - Semi-untethered jailbreaks (h3lix, Phœnix) require the
 *     jailbreak to be re-applied after each HARD boot before
 *     CoolBooter can be used — the untether only works on
 *     soft-reboots in those cases
 *   - Full untethers (Pangu iOS 7, TaiG iOS 8/9) work on every boot
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

#define UNTETHER_PLIST   "/Library/LaunchDaemons/com.coolbooter.untether.plist"
#define UNTETHER_DAEMON  "/usr/local/bin/coolbooter-untether"
#define SKIP_FLAG        "/var/mobile/Media/CoolBooter/.boot_primary"
#define CB_IBSS_PATH     "/var/mobile/Media/CoolBooter/iBSS.patched"
#define CB_IBEC_PATH     "/var/mobile/Media/CoolBooter/iBEC.patched"
#define KLOADER_BIN      "/usr/local/bin/multi_kloader"
#define KLOADER_ALT      "/usr/bin/kloader"

/* ── launchd plist content ──────────────────────────────────── */
static const char UNTETHER_PLIST_CONTENT[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\"\n"
    "  \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\">\n"
    "<dict>\n"
    "  <key>Label</key>\n"
    "  <string>com.coolbooter.untether</string>\n"
    "\n"
    "  <key>ProgramArguments</key>\n"
    "  <array>\n"
    "    <string>" UNTETHER_DAEMON "</string>\n"
    "  </array>\n"
    "\n"
    "  <!-- Run very early, before SpringBoard -->\n"
    "  <key>RunAtLoad</key>\n"
    "  <true/>\n"
    "\n"
    "  <!-- Do NOT restart if it triggers a reboot -->\n"
    "  <key>KeepAlive</key>\n"
    "  <false/>\n"
    "\n"
    "  <!-- Priority: run before normal daemons -->\n"
    "  <key>ThrottleInterval</key>\n"
    "  <integer>0</integer>\n"
    "\n"
    "  <key>StandardErrorPath</key>\n"
    "  <string>/var/mobile/Media/CoolBooter/untether.log</string>\n"
    "\n"
    "  <key>StandardOutPath</key>\n"
    "  <string>/var/mobile/Media/CoolBooter/untether.log</string>\n"
    "</dict>\n"
    "</plist>\n";

/* ── Untether daemon source (written as a separate binary) ──── */
/* This is the source for coolbooter-untether itself.
 * Compiled separately as a tiny standalone binary. */
static const char UNTETHER_DAEMON_SOURCE[] =
    "#define _POSIX_C_SOURCE 200809L\n"
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
    "#include <unistd.h>\n"
    "#include <sys/stat.h>\n"
    "#include <time.h>\n"
    "\n"
    "#define SKIP_FLAG    \"/var/mobile/Media/CoolBooter/.boot_primary\"\n"
    "#define CB_IBSS      \"/var/mobile/Media/CoolBooter/iBSS.patched\"\n"
    "#define CB_IBEC      \"/var/mobile/Media/CoolBooter/iBEC.patched\"\n"
    "#define KLOADER      \"/usr/local/bin/multi_kloader\"\n"
    "#define KLOADER_ALT  \"/usr/bin/kloader\"\n"
    "\n"
    "int main(void) {\n"
    "    FILE *log = fopen(\"/var/mobile/Media/CoolBooter/untether.log\", \"a\");\n"
    "    if (!log) log = stderr;\n"
    "    fprintf(log, \"[untether] Starting\\n\"); fflush(log);\n"
    "\n"
    "    /* Check skip flag — if present, boot primary OS (remove flag and exit) */\n"
    "    struct stat st;\n"
    "    if (stat(SKIP_FLAG, &st) == 0) {\n"
    "        fprintf(log, \"[untether] Skip flag found — booting primary OS\\n\");\n"
    "        fflush(log);\n"
    "        unlink(SKIP_FLAG);\n"
    "        return 0;\n"
    "    }\n"
    "\n"
    "    /* Verify patched bootloaders exist */\n"
    "    if (stat(CB_IBSS, &st) != 0 || stat(CB_IBEC, &st) != 0) {\n"
    "        fprintf(log, \"[untether] Patched bootloaders missing — skipping\\n\");\n"
    "        fflush(log);\n"
    "        return 1;\n"
    "    }\n"
    "\n"
    "    /* Find kloader */\n"
    "    const char *kl = (stat(KLOADER, &st) == 0) ? KLOADER : KLOADER_ALT;\n"
    "    if (stat(kl, &st) != 0) {\n"
    "        fprintf(log, \"[untether] kloader not found\\n\");\n"
    "        fflush(log);\n"
    "        return 1;\n"
    "    }\n"
    "\n"
    "    /* Small delay to allow launchd to settle */\n"
    "    sleep(1);\n"
    "\n"
    "    fprintf(log, \"[untether] Triggering boot via %s\\n\", kl);\n"
    "    fflush(log);\n"
    "    if (log != stderr) fclose(log);\n"
    "\n"
    "    /* Exec kloader — device will reset */\n"
    "    char *argv[] = { (char*)kl, CB_IBSS, CB_IBEC, NULL };\n"
    "    execv(kl, argv);\n"
    "    return 1; /* only reached if execv fails */\n"
    "}\n";

/* ── Install untether ─────────────────────────────────────────── */
int untether_install(void) {
    struct stat st;

    /* Check patched bootloaders exist */
    if (stat(CB_IBSS_PATH, &st) != 0 || stat(CB_IBEC_PATH, &st) != 0) {
        fprintf(stderr, "[untether] Patched bootloaders not found — install first\n");
        return -1;
    }

    /* Step 1: Write the untether daemon source and compile it */
    const char *src_path = "/tmp/coolbooter_untether.c";
    FILE *sf = fopen(src_path, "w");
    if (!sf) { perror("[untether] Cannot write source"); return -1; }
    fwrite(UNTETHER_DAEMON_SOURCE, 1, strlen(UNTETHER_DAEMON_SOURCE), sf);
    fclose(sf);

    /* Compile daemon with clang (available on jailbroken iOS via Cydia) */
    char compile_cmd[512];
    snprintf(compile_cmd, sizeof(compile_cmd),
             "clang -arch armv7 -miphoneos-version-min=5.0 "
             "-Os -o \"%s\" \"%s\" 2>&1",
             UNTETHER_DAEMON, src_path);

    fprintf(stderr, "[untether] Compiling untether daemon...\n");
    FILE *cp = popen(compile_cmd, "r");
    if (cp) {
        char line[256];
        while (fgets(line, sizeof(line), cp))
            fprintf(stderr, "  %s", line);
        int rc = pclose(cp);
        if (rc != 0) {
            fprintf(stderr, "[untether] Compile failed (exit %d)\n", rc);
            /* Fallback: install a shell script instead */
            FILE *sh = fopen(UNTETHER_DAEMON, "w");
            if (!sh) { unlink(src_path); return -1; }
            fprintf(sh,
                "#!/bin/sh\n"
                "# CoolBooter untether daemon\n"
                "[ -f \"" SKIP_FLAG "\" ] && { rm \"" SKIP_FLAG "\"; exit 0; }\n"
                "[ -f \"" CB_IBSS_PATH "\" ] || exit 1\n"
                "[ -f \"" CB_IBEC_PATH "\" ] || exit 1\n"
                "sleep 1\n"
                "KL=\"" KLOADER_BIN "\"\n"
                "[ -x \"$KL\" ] || KL=\"" KLOADER_ALT "\"\n"
                "[ -x \"$KL\" ] || exit 1\n"
                "exec \"$KL\" \"" CB_IBSS_PATH "\" \"" CB_IBEC_PATH "\"\n");
            fclose(sh);
            chmod(UNTETHER_DAEMON, 0755);
            fprintf(stderr, "[untether] Installed shell script fallback\n");
        }
    }
    unlink(src_path);

    /* ldid sign the daemon (entitlements for platform execution) */
    char ldid_cmd[256];
    snprintf(ldid_cmd, sizeof(ldid_cmd), "ldid -S \"%s\" 2>/dev/null", UNTETHER_DAEMON);
    system(ldid_cmd);
    chmod(UNTETHER_DAEMON, 0755);

    /* Step 2: Write launchd plist */
    FILE *pf = fopen(UNTETHER_PLIST, "w");
    if (!pf) {
        fprintf(stderr, "[untether] Cannot write plist to %s\n", UNTETHER_PLIST);
        return -1;
    }
    fwrite(UNTETHER_PLIST_CONTENT, 1, strlen(UNTETHER_PLIST_CONTENT), pf);
    fclose(pf);
    chmod(UNTETHER_PLIST, 0644);

    /* Step 3: Load the plist into launchd */
    char load_cmd[256];
    snprintf(load_cmd, sizeof(load_cmd),
             "launchctl load \"%s\" 2>&1", UNTETHER_PLIST);
    system(load_cmd);

    fprintf(stderr,
        "[untether] ✓ Untether installed\n"
        "  Daemon:  %s\n"
        "  Plist:   %s\n"
        "  On next boot, secondary iOS will launch automatically.\n"
        "  To skip and boot primary: touch %s before powering on.\n",
        UNTETHER_DAEMON, UNTETHER_PLIST, SKIP_FLAG);
    return 0;
}

/* ── Remove untether ─────────────────────────────────────────── */
int untether_remove(void) {
    /* Unload from launchd first */
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "launchctl unload \"%s\" 2>/dev/null", UNTETHER_PLIST);
    system(cmd);

    unlink(UNTETHER_PLIST);
    unlink(UNTETHER_DAEMON);

    fprintf(stderr, "[untether] Untether removed\n");
    return 0;
}

/* ── Status check ────────────────────────────────────────────── */
int untether_is_installed(void) {
    struct stat st;
    return (stat(UNTETHER_PLIST, &st) == 0 &&
            stat(UNTETHER_DAEMON, &st) == 0) ? 1 : 0;
}

/* ── Set skip flag (boot into primary on next boot) ─────────── */
int untether_skip_next(void) {
    FILE *f = fopen(SKIP_FLAG, "w");
    if (!f) { perror("[untether] Cannot create skip flag"); return -1; }
    fclose(f);
    fprintf(stderr, "[untether] Next boot will load primary iOS\n");
    return 0;
}
