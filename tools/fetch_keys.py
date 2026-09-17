#!/usr/bin/env python3
"""
tools/fetch_keys.py — Fetch firmware keys from TheIphoneWiki
and generate devices.h CB_FIRMWARE_DB entries.

Usage:
    python3 fetch_keys.py --product iPhone4,1 --version 6.1.3
    python3 fetch_keys.py --all-supported   # fetch all supported combos
    python3 fetch_keys.py --output include/devices_keys.h

Requires: requests, beautifulsoup4
    pip3 install requests beautifulsoup4
"""

import sys
import re
import json
import time
import argparse
from pathlib import Path

try:
    import requests
    from bs4 import BeautifulSoup
except ImportError:
    print("Install deps: pip3 install requests beautifulsoup4", file=sys.stderr)
    sys.exit(1)

# ── Supported device/version matrix ──────────────────────────────
SUPPORTED = [
    # (product,      version,  build)
    ("iPhone3,1",  "6.1.3",  "10B329"),
    ("iPhone3,2",  "6.1.3",  "10B329"),
    ("iPhone3,3",  "6.1.3",  "10B329"),
    ("iPhone4,1",  "6.1.3",  "10B329"),
    ("iPhone4,1",  "7.1.2",  "11D257"),
    ("iPhone4,1",  "8.4.1",  "12H321"),
    ("iPhone4,1",  "9.3.5",  "13G36"),
    ("iPhone5,1",  "6.1.4",  "10B350"),
    ("iPhone5,1",  "7.1.2",  "11D257"),
    ("iPhone5,1",  "8.4.1",  "12H321"),
    ("iPhone5,1",  "9.3.5",  "13G36"),
    ("iPhone5,2",  "6.1.4",  "10B350"),
    ("iPhone5,2",  "9.3.5",  "13G36"),
    ("iPhone5,3",  "7.1.2",  "11D257"),
    ("iPhone5,3",  "9.3.5",  "13G36"),
    ("iPhone5,4",  "9.3.5",  "13G36"),
    ("iPad2,1",    "6.1.3",  "10B329"),
    ("iPad2,1",    "9.3.5",  "13G36"),
    ("iPad2,4",    "9.3.5",  "13G36"),
    ("iPad2,5",    "9.3.5",  "13G36"),
    ("iPad3,1",    "9.3.5",  "13G36"),
    ("iPad3,4",    "9.3.5",  "13G36"),
    ("iPod5,1",    "6.1.6",  "10B500"),
    ("iPod5,1",    "9.3.5",  "13G36"),
]

WIKI_BASE = "https://www.theiphonewiki.com/wiki/Firmware_Keys"

# ── Fetch from wiki ───────────────────────────────────────────────
def get_wiki_keys(product: str, version: str, build: str) -> dict | None:
    """
    Fetch key page from TheIphoneWiki.
    URL format: /wiki/Firmware_Keys/<Build>_<HWModel>
    We need to map product → HW model first.
    """
    # Map product → wiki page name pattern
    PRODUCT_PAGE = {
        "iPhone3,1": "N90AP",  "iPhone3,2": "N90BAP",
        "iPhone3,3": "N92AP",  "iPhone4,1": "N94AP",
        "iPhone5,1": "N41AP",  "iPhone5,2": "N42AP",
        "iPhone5,3": "N48AP",  "iPhone5,4": "N49AP",
        "iPad2,1":   "K93AP",  "iPad2,4":   "K93AAP",
        "iPad2,5":   "P105AP", "iPad3,1":   "J1AP",
        "iPad3,4":   "P101AP", "iPod5,1":   "N78AP",
    }

    hw = PRODUCT_PAGE.get(product)
    if not hw:
        print(f"  No HW model mapping for {product}", file=sys.stderr)
        return None

    # TheIphoneWiki URL: /wiki/<Build>_(<HWModel>)
    url = f"https://www.theiphonewiki.com/wiki/{build}_({hw})"
    print(f"  Fetching: {url}", file=sys.stderr)

    try:
        resp = requests.get(url, timeout=15, headers={
            "User-Agent": "coolbooter-oss/keyfetcher (educational/research)"
        })
        if resp.status_code == 404:
            print(f"  Not found: {url}", file=sys.stderr)
            return None
        resp.raise_for_status()
    except Exception as e:
        print(f"  Error: {e}", file=sys.stderr)
        return None

    soup = BeautifulSoup(resp.text, "html.parser")
    keys = {"product": product, "version": version, "build": build,
            "hw_model": hw}

    # Parse the firmware key table
    # TheIphoneWiki uses a standard table format:
    # Component | Filename | IV | Key
    table = soup.find("table", class_="wikitable")
    if not table:
        print(f"  No key table found at {url}", file=sys.stderr)
        return None

    def clean(text):
        return re.sub(r'\s+', '', text.strip().lower())

    for row in table.find_all("tr")[1:]:  # skip header
        cells = [td.get_text(strip=True) for td in row.find_all(["td", "th"])]
        if len(cells) < 3:
            continue

        component = cells[0].lower()
        if "ibss" in component:
            if len(cells) >= 4:
                keys["ibss_iv"]  = clean(cells[-2])
                keys["ibss_key"] = clean(cells[-1])
        elif "ibec" in component:
            if len(cells) >= 4:
                keys["ibec_iv"]  = clean(cells[-2])
                keys["ibec_key"] = clean(cells[-1])
        elif "kernelcache" in component or "kernel" in component:
            if len(cells) >= 4:
                keys["kernel_iv"]  = clean(cells[-2])
                keys["kernel_key"] = clean(cells[-1])
        elif "rootfs" in component or "root filesystem" in component:
            if len(cells) >= 2:
                rk = clean(cells[-1])
                if re.match(r'^[0-9a-f]{64}$', rk):
                    keys["rootfs_key"] = rk

    # Fetch IPSW URL and SHA1 from the same page
    for link in soup.find_all("a", href=True):
        href = link["href"]
        if ".ipsw" in href and ("apple.com" in href or "cdn-apple" in href):
            keys["ipsw_url"] = href
            break

    return keys


# ── Generate C struct entry ───────────────────────────────────────
def make_c_entry(k: dict) -> str:
    def cstr(v):
        return f'"{v}"' if v else "NULL"

    return f"""\
    /* {k['product']} — iOS {k['version']} ({k['build']}) */
    {{
        .product         = "{k['product']}",
        .ios_version     = "{k['version']}",
        .build           = "{k['build']}",
        .ibss_iv         = {cstr(k.get('ibss_iv'))},
        .ibss_key        = {cstr(k.get('ibss_key'))},
        .ibec_iv         = {cstr(k.get('ibec_iv'))},
        .ibec_key        = {cstr(k.get('ibec_key'))},
        .kernelcache_iv  = {cstr(k.get('kernel_iv'))},
        .kernelcache_key = {cstr(k.get('kernel_key'))},
        .rootfs_key      = {cstr(k.get('rootfs_key'))},
        .ipsw_url        = {cstr(k.get('ipsw_url'))},
        .ipsw_size       = 0,
        .ipsw_sha1       = NULL,
    }},"""


# ── Main ──────────────────────────────────────────────────────────
def main():
    p = argparse.ArgumentParser(description="Fetch firmware keys for devices.h")
    p.add_argument("--product", help="Single product (e.g. iPhone4,1)")
    p.add_argument("--version", help="iOS version (e.g. 6.1.3)")
    p.add_argument("--build",   help="Build number (e.g. 10B329)")
    p.add_argument("--all",     action="store_true",
                   help="Fetch all supported device/version combos")
    p.add_argument("--output",  default=None,
                   help="Output header file (default: stdout)")
    args = p.parse_args()

    targets = []
    if args.all:
        targets = SUPPORTED
    elif args.product and args.version and args.build:
        targets = [(args.product, args.version, args.build)]
    else:
        print("Use --product/--version/--build or --all", file=sys.stderr)
        p.print_help()
        sys.exit(1)

    entries = []
    for product, version, build in targets:
        print(f"[fetch] {product} {version} ({build})", file=sys.stderr)
        k = get_wiki_keys(product, version, build)
        if k:
            entries.append(make_c_entry(k))
        else:
            print(f"  SKIPPED (no data)\n", file=sys.stderr)
        time.sleep(0.5)  # be polite to the wiki

    output = []
    output.append("/* AUTO-GENERATED by tools/fetch_keys.py — do not edit by hand */")
    output.append("/* Keys sourced from: https://www.theiphonewiki.com/wiki/Firmware_Keys */")
    output.append("")
    output.append("/* Append these entries to CB_FIRMWARE_DB[] in include/devices.h */")
    output.append("")
    for e in entries:
        output.append(e)
        output.append("")

    result = "\n".join(output)

    if args.output:
        Path(args.output).write_text(result)
        print(f"Written to {args.output}", file=sys.stderr)
    else:
        print(result)


if __name__ == "__main__":
    main()
