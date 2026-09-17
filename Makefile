# ============================================================
# Makefile — CoolBooter CLI (Open Source)
# Build system: Theos (https://theos.dev) or iOS SDK direct
#
# Quick start (with Theos installed):
#   make
#   make package          <- creates .deb
#   make install          <- installs to connected device via SSH
#
# Cross-compile without Theos (requires osxcross or XCode + SDK):
#   make SYSROOT=/path/to/iPhoneOS.sdk ARCH=armv7
# ============================================================

THEOS_DEVICE_IP ?= 192.168.1.100
THEOS_DEVICE_PORT ?= 22

include $(THEOS)/makefiles/common.mk

# ── Tool target ──────────────────────────────────────────────
TOOL_NAME = coolbootercli

coolbootercli_FILES  = src/main.c \
                       src/img3.c \
                       src/ipsw.c \
                       src/patcher.c \
                       src/partition.c \
                       src/restore.c

coolbootercli_CFLAGS = -I$(THEOS_PROJECT_DIR)/include \
                       -std=c11 \
                       -Wall -Wextra \
                       -D_GNU_SOURCE \
                       -DHAVE_OPENSSL=0 \
                       -O2

# Link against IOKit for GID key decryption (optional)
coolbootercli_LDFLAGS = -framework IOKit \
                        -framework CoreFoundation \
                        -lz

# Minimum iOS deployment target (5.0 for broadest support)
coolbootercli_DEPLOYMENT_TARGET = 5.0

# Architecture: armv7 + armv7s for all 32-bit devices
ARCHS = armv7 armv7s

include $(THEOS)/makefiles/tool.mk

# ── Manual cross-compile (without Theos) ─────────────────────
# Usage: make cross SYSROOT=/path/to/sdk
ARCH       ?= armv7
SYSROOT    ?= /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk
CC         = xcrun -sdk iphoneos clang
CFLAGS_CROSS = -arch $(ARCH) \
               -isysroot $(SYSROOT) \
               -miphoneos-version-min=5.0 \
               -I./include \
               -std=c11 -Wall -O2

SRCS = src/main.c src/img3.c src/ipsw.c src/patcher.c \
       src/partition.c src/restore.c

.PHONY: cross clean-cross

cross:
	$(CC) $(CFLAGS_CROSS) $(SRCS) \
	    -framework IOKit -framework CoreFoundation -lz \
	    -o coolbootercli_$(ARCH)
	ldid -S coolbootercli_$(ARCH)
	@echo "Built: coolbootercli_$(ARCH)"

clean-cross:
	rm -f coolbootercli_armv7 coolbootercli_armv7s
