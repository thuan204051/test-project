# CoolBooter CLI — Open Source Reimplementation

Clean-room reimplementation of CoolBooter CLI based on:
- Documented tool behavior and public changelogs
- Open-source dependencies (ios-kexec-utils, gptfdisk fork)
- The Apple IMG3/IMG4 firmware format (public documentation)
- Jailbreak community knowledge of iBSS/iBEC patch patterns

## Architecture

```
coolbootercli [args]
      │
      ├─ Phase 1: Firmware
      │     ├─ Download IPSW (curl → Apple CDN)
      │     ├─ Extract IPSW (unzip)
      │     └─ Decrypt iBSS/iBEC (AES-128-CBC, public keys)
      │
      ├─ Phase 2: Patch
      │     ├─ iBSS: signature check bypass (Thumb2 NOP patch)
      │     └─ iBEC: sig bypass + rd=disk0s1s2→s3 + optional -v
      │
      ├─ Phase 3: Partition
      │     ├─ Backup GPT/LwVM table
      │     ├─ Shrink primary system partition
      │     └─ Create /dev/disk0s1s3
      │
      ├─ Phase 4: Restore
      │     ├─ ASR restore rootfs DMG → disk0s1s3
      │     ├─ Copy activation records
      │     ├─ Data protection fix (iOS 9+ host)
      │     └─ Optional: jailbreak secondary
      │
      └─ Phase 5: Boot
            └─ multi_kloader iBSS.patched iBEC.patched
                    → platform reset → secondary iOS boots
```

## Supported Devices

| Device | Chip | Host iOS | Secondary iOS |
|--------|------|----------|---------------|
| iPhone 4 (GSM/CDMA) | A4 (s5l8930x) | 7.x | 4.0–6.1.3 |
| iPhone 4S | A5 (s5l8940x) | 5–9.3.5 | 5.0–9.3.5 |
| iPhone 5/5c | A6 (s5l8950x) | 6–10.3.4 | 6.0–10.3.4 |
| iPad 2 | A5 (s5l8940x) | 4–9.3.5 | 5.0–9.3.5 |
| iPad 3 | A5X (s5l8945x) | 5–9.3.5 | 5.0–9.3.5 |
| iPad 4 | A6X (s5l8955x) | 6–10.3.4 | 6.0–10.3.4 |
| iPad mini 1G | A5 (s5l8942x) | 6–9.3.5 | 6.0–9.3.5 |
| iPod touch 5G | A5 (s5l8942x) | 5–9.3.5 | 5.0–9.3.5 |

## Prerequisites

1. **Jailbreak with kernel access:**
   - `tfp0` (task_for_pid 0): Pangu, TaiG, 3uTools, Yalu102
   - `hgsp4` (host_get_special_port 4): h3lix (iOS 10), Phœnix (iOS 9.3.5)

2. **multi_kloader** — boot trigger tool:
   ```
   # Install from ios-kexec-utils (via SSH):
   # https://github.com/jonathanseals/ios-kexec-utils
   ```

3. **gptfdisk** — LwVM-aware partition tool:
   ```
   # Provided at coolbooter.com/gptfdisk.zip (original fork)
   # Install to /usr/local/bin/gptfdisk
   ```

4. **Free NAND space:** ≥2GB recommended

## Build

### With Theos
```bash
export THEOS=/opt/theos
make
make package
```

### Cross-compile (macOS + Xcode)
```bash
make cross ARCH=armv7
# Output: coolbootercli_armv7
```

### Install to device
```bash
# Via SSH (device IP):
scp coolbootercli_armv7 root@<device-ip>:/usr/local/bin/coolbootercli
ssh root@<device-ip> chmod +x /usr/local/bin/coolbootercli
```

## Usage

```bash
# Install iOS 6.1.3 on iPhone 4S (jailbroken, with auto jailbreak on secondary)
coolbootercli -t 6.1.3 -j

# Install from local IPSW with verbose boot
coolbootercli -t 6.1.3 -i /var/mobile/iPhone4,1_6.1.3.ipsw -v

# Install with explicit iBSS/iBEC keys (if not in built-in DB)
coolbootercli -t 7.1.2 \
  --ibss-key 1a2b3c4d... --ibss-iv 0f1e2d3c... \
  --ibec-key 5e6f7a8b... --ibec-iv 4c5d6e7f...

# Boot existing secondary OS
coolbootercli -b

# Remove secondary OS
coolbootercli -r
```

## Key Database

Firmware decryption keys are required for each device+version combination.
The built-in database (`include/devices.h`) contains a subset of known keys.

For missing keys, consult:
- https://www.theiphonewiki.com/wiki/Firmware_Keys
- Add entries to `CB_FIRMWARE_DB[]` in `include/devices.h`

## How iBSS/iBEC Patching Works

```
Decrypted iBSS / iBEC (ARM Thumb2 binary):

  Signature verification function:
  ┌─────────────────────────────────────┐
  │  BL  _verify_shsh_signature         │
  │  CMP  R0, #0      ; check result    │
  │  BNE  fail_handler ; branch if fail │  ← patch: NOP this branch
  │  ...continue...                     │
  └─────────────────────────────────────┘

  Boot partition string (in iBEC only):
  ┌─────────────────────────────────────┐
  │  "rd=disk0s1s2 -progress"           │ ← patch: s2 → s3
  │  "/dev/disk0s1s2"                   │ ← patch: s2 → s3
  └─────────────────────────────────────┘
```

## Boot Flow After Installation

```
Power on
  └─ BootROM (read-only, can't patch)
       └─ Loads patched iBSS from SRAM
            └─ iBSS loads patched iBEC
                 └─ iBEC passes boot-args: rd=disk0s1s3
                      └─ XNU kernel mounts /dev/disk0s1s3 as root
                           └─ Secondary iOS boots
```

## Files Created on Device

```
/var/mobile/Media/CoolBooter/
├── iBSS.patched          ← patched bootloader (loaded by kloader)
├── iBEC.patched          ← patched bootloader
├── backup_gpt.bin        ← partition table backup
├── ipsw/                 ← cached IPSW files
└── extracted/            ← extracted IPSW components (temp)
```

## Contributing

This is a clean-room reimplementation. PRs welcome for:
- Additional device/firmware key entries in `devices.h`
- Improved patch pattern detection in `patcher.c`
- IMG4 support (for iOS 10+ targets on A6)
- Better error recovery in partition operations

## License

MIT License. See LICENSE file.

## Disclaimer

This tool modifies partition tables on NAND flash storage.
Incorrect use can result in data loss or an unbootable device.
Use only on devices you own and can restore. Back up all data first.
The authors are not responsible for any damage or data loss.
