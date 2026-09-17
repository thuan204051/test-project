#ifndef UNTETHER_H
#define UNTETHER_H

/* ============================================================
 * untether.h — Automatic secondary OS boot at device startup
 *
 * Installs a launchd daemon on the primary OS that calls
 * multi_kloader before SpringBoard, redirecting boot to secondary.
 *
 * To boot primary: create /var/mobile/Media/CoolBooter/.boot_primary
 *   (the untether removes this flag after reading it)
 * ============================================================ */

/* Install untether daemon + launchd plist. Returns 0 on success. */
int untether_install(void);

/* Remove untether daemon + launchd plist. Returns 0 on success. */
int untether_remove(void);

/* Check if untether is currently installed. Returns 1/0. */
int untether_is_installed(void);

/* Create the skip flag so next boot goes to primary OS. */
int untether_skip_next(void);

#endif /* UNTETHER_H */
