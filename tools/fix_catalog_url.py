#!/usr/bin/env python3
"""
fix_catalog_url.py — One-shot script to regenerate firmware_catalog.json
with correct raw.githubusercontent.com download URLs and push to GitHub.

Usage:  python3 tools/fix_catalog_url.py
"""
import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from store_core import FirmwareCatalog, scan_bins, make_url, get_raw_base, git_commit_push

STORE_REPO = os.path.expanduser(
    "~/Projects/Project_Personal/ESP_32_Custom_Launcher/Store_Repo"
)

def main():
    if not os.path.isdir(STORE_REPO):
        print(f"[ERROR] Store repo not found at: {STORE_REPO}")
        print("        Clone it first or update STORE_REPO path in this script.")
        sys.exit(1)

    raw_base = get_raw_base(STORE_REPO)
    if not raw_base:
        print("[ERROR] Could not determine GitHub raw base URL from git remote.")
        print("        Make sure 'origin' is set to a GitHub remote in the store repo.")
        sys.exit(1)

    print(f"[INFO] Raw base URL: {raw_base}")

    catalog = FirmwareCatalog(STORE_REPO)
    bins    = scan_bins(STORE_REPO)

    if not bins:
        print("[WARN] No .bin files found in firmwares/ — nothing to do.")
        sys.exit(0)

    updated = 0
    for b in bins:
        full_url = f"{raw_base}/{b['rel_path']}"
        existing = catalog.get(b["id"])

        entry = {
            "id":          b["id"],
            "name":        b["filename"],
            "version":     existing.get("version", "v1.0") if existing else "v1.0",
            "description": existing.get("description", "Auto-discovered binary") if existing else "Auto-discovered binary",
            "size_bytes":  b["size"],
            "url":         full_url,
        }

        old_url = existing.get("url", "") if existing else ""
        if old_url != full_url:
            print(f"[FIX]  {b['filename']}")
            print(f"       OLD url: '{old_url}'")
            print(f"       NEW url: '{full_url}'")
            updated += 1
        else:
            print(f"[OK]   {b['filename']} — URL already correct")

        catalog.add_or_update(entry)

    if updated == 0:
        print("\n[INFO] No URL changes needed. Catalog is already correct.")
        return

    print(f"\n[INFO] Updated {updated} entry/entries. Pushing to GitHub...")
    ok, out = git_commit_push(STORE_REPO, f"fix: populate download URLs for {updated} firmware(s)")
    if ok:
        print(f"[OK]   Pushed successfully.\n{out}")
    else:
        print(f"[ERROR] Git push failed:\n{out}")
        print("\n       The firmware_catalog.json has been updated locally.")
        print("       Run: cd ~/...Store_Repo && git add . && git commit -m 'fix urls' && git push")
        sys.exit(1)

if __name__ == "__main__":
    main()
