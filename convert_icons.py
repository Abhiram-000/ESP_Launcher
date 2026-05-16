#!/usr/bin/env python3
"""
convert_icons.py — ESP32-S3 Launcher Icon Asset Pipeline
=========================================================
Purpose:
    Convert PNG icons into flash-resident RGB565 C arrays for direct
    use with LovyanGFX's tft.pushImage() / canvas->pushImage().

Responsibilities:
    - Read PNGs from the 'icons/' directory (project root)
    - Resize to TARGET_SIZE×TARGET_SIZE using high-quality bicubic interpolation
    - Composite transparency onto BG_COLOR background
    - Convert to RGB565 (5-6-5, no byte swapping, ESP32 native format)
    - Write one '.h' file per icon into 'src/assets/icons/'
    - Skip regeneration when source PNG is unchanged (SHA256 hash cache)
    - Run automatically as a PlatformIO pre-build script

Dependencies:
    - Pillow (pip install Pillow)
    - hashlib, json (stdlib)

Usage (standalone):
    python3 convert_icons.py

Usage (PlatformIO pre-build):
    Invoked automatically via extra_scripts = pre:convert_icons.py
"""

import os
import sys
import json
import hashlib

try:
    from PIL import Image
except ImportError:
    print("[icons] ERROR: Pillow not installed. Run: pip install Pillow")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# Input PNG directory (relative to this script's location)
INPUT_DIR = "icons"

# Output header directory
OUTPUT_DIR = os.path.join("src", "assets", "icons")

# Target icon resolution stored in flash (64×64 = 4096 pixels)
TARGET_SIZE = 64

# Background colour for alpha compositing — must match the UI theme background
# RGB(2, 3, 5) is the near-black used by the Cyberpunk theme base.
BG_COLOR = (2, 3, 5)

# Hash cache file — tracks which PNGs have changed to avoid redundant rebuilds
HASH_CACHE_FILE = ".icon_hashes.json"


# ---------------------------------------------------------------------------
# Core conversion
# ---------------------------------------------------------------------------

def _file_sha256(path: str) -> str:
    """Return the SHA256 hex digest of a file's contents."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def _safe_c_identifier(name: str) -> str:
    """
    Sanitise a filename stem into a valid C identifier.
    Replaces any non-alphanumeric character with '_' and ensures the
    identifier does not start with a digit.
    """
    ident = ""
    for ch in name:
        ident += ch if (ch.isalnum() or ch == "_") else "_"
    if ident and ident[0].isdigit():
        ident = "_" + ident
    return ident.lower()


def png_to_rgb565_header(in_file: str, out_file: str, array_name: str) -> bool:
    """
    Convert a single PNG to an RGB565 C header.

    The generated array contains TARGET_SIZE×TARGET_SIZE uint16_t values
    in ESP32 native (little-endian) word format.  RGB565 bit layout:
        bits [15:11] → R (5 bits, MSB)
        bits [10: 5] → G (6 bits)
        bits [ 4: 0] → B (5 bits, LSB)

    No byte-swapping is performed.  LovyanGFX pushImage() consumes the
    data exactly as stored.

    Returns True on success, False on error.
    """
    # --- Open source image -------------------------------------------------
    try:
        img = Image.open(in_file)
    except Exception as exc:
        print(f"[icons] ERROR  Cannot open '{in_file}': {exc}")
        return False

    # --- Alpha compositing -------------------------------------------------
    # Forces a solid colour background for any semi-transparent pixels so
    # that artefacts do not appear on the display.
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        bg = Image.new("RGB", img.size, BG_COLOR)
        bg.paste(img, mask=img.split()[3])   # alpha channel as mask
        img = bg
    else:
        img = img.convert("RGB")

    # --- Resize ------------------------------------------------------------
    try:
        resample = Image.Resampling.BICUBIC
    except AttributeError:
        resample = Image.BICUBIC  # Pillow < 9

    img = img.resize((TARGET_SIZE, TARGET_SIZE), resample)

    # --- RGB565 conversion -------------------------------------------------
    pixels = list(img.getdata())
    rgb565 = []
    for r, g, b in pixels:
        # R:5  G:6  B:5 — no byte swap, ESP32 native word format
        val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        rgb565.append(val)

    # --- Write header ------------------------------------------------------
    num_pixels = TARGET_SIZE * TARGET_SIZE   # 4096 for 64×64

    try:
        with open(out_file, "w") as f:
            f.write("#pragma once\n")
            f.write("#include <stdint.h>\n")
            f.write("\n")
            f.write(f"/* {array_name}\n")
            f.write(f" * Source  : {os.path.basename(in_file)}\n")
            f.write(f" * Size    : {TARGET_SIZE}x{TARGET_SIZE} pixels, RGB565\n")
            f.write(f" * Format  : uint16_t, R[15:11] G[10:5] B[4:0], no byte-swap\n")
            f.write(f" * Usage   : tft.pushImage(x, y, {TARGET_SIZE}, {TARGET_SIZE}, {array_name});\n")
            f.write( " * Note    : Flash-resident; zero runtime heap usage.\n")
            f.write( " */\n")
            f.write("\n")
            f.write(
                f"static const uint16_t {array_name}[{num_pixels}]"
                f" __attribute__((aligned(4))) = {{\n"
            )

            cols_per_row = 12   # 12 hex values per source line = readable width
            for i, val in enumerate(rgb565):
                f.write(f"0x{val:04X},")
                if (i + 1) % cols_per_row == 0:
                    f.write("\n")
                else:
                    f.write(" ")

            # Ensure file ends with a complete closing brace on its own line
            f.write("\n};\n")

    except OSError as exc:
        print(f"[icons] ERROR  Cannot write '{out_file}': {exc}")
        return False

    print(f"[icons] OK     {os.path.basename(in_file):30s} → {out_file}")
    return True


# ---------------------------------------------------------------------------
# Pipeline
# ---------------------------------------------------------------------------

def _load_hash_cache(cache_path: str) -> dict:
    """Load the SHA256 hash cache from disk.  Returns empty dict on failure."""
    if os.path.exists(cache_path):
        try:
            with open(cache_path, "r") as f:
                return json.load(f)
        except (json.JSONDecodeError, OSError):
            pass
    return {}


def _save_hash_cache(cache_path: str, cache: dict) -> None:
    """Persist the hash cache to disk."""
    try:
        with open(cache_path, "w") as f:
            json.dump(cache, f, indent=2)
    except OSError as exc:
        print(f"[icons] WARN   Could not save hash cache: {exc}")


def run_pipeline(base_dir: str) -> int:
    """
    Execute the full icon conversion pipeline.

    Args:
        base_dir: Absolute or relative path to use as the working root
                  (the directory that contains 'icons/' and 'src/').

    Returns:
        Number of icons successfully converted (0 if nothing changed).
    """
    input_dir = os.path.join(base_dir, INPUT_DIR)
    output_dir = os.path.join(base_dir, OUTPUT_DIR)
    cache_path = os.path.join(base_dir, HASH_CACHE_FILE)

    if not os.path.isdir(input_dir):
        print(f"[icons] ERROR  Input folder '{input_dir}' not found.")
        print(f"[icons]        Create an 'icons/' directory and place .png files inside.")
        return 0

    os.makedirs(output_dir, exist_ok=True)

    hash_cache = _load_hash_cache(cache_path)
    converted = 0
    skipped = 0

    for filename in sorted(os.listdir(input_dir)):
        if not filename.lower().endswith(".png"):
            continue

        in_path = os.path.join(input_dir, filename)
        stem = os.path.splitext(filename)[0]
        array_name = f"icon_{_safe_c_identifier(stem)}"
        out_path = os.path.join(output_dir, f"{stem}.h")

        # --- Incremental rebuild check ------------------------------------
        current_hash = _file_sha256(in_path)
        cached_hash = hash_cache.get(filename)

        if cached_hash == current_hash and os.path.exists(out_path):
            print(f"[icons] SKIP   {filename:30s} (unchanged)")
            skipped += 1
            continue

        # --- Convert ------------------------------------------------------
        if png_to_rgb565_header(in_path, out_path, array_name):
            hash_cache[filename] = current_hash
            converted += 1

    _save_hash_cache(cache_path, hash_cache)

    total = converted + skipped
    print(f"\n[icons] Pipeline complete: {converted} converted, {skipped} skipped, {total} total icons.")
    return converted


# ---------------------------------------------------------------------------
# PlatformIO pre-build entry point
# ---------------------------------------------------------------------------

# PlatformIO imports this module and calls the SConscript environment
# if 'env' is defined in the global scope.  We check here so the file
# works both as a standalone script and as a PlatformIO extra_script.
try:
    Import("env")  # type: ignore  # noqa: F821  (PlatformIO SConscript global)

    def _pio_pre_build(source, target, env):  # noqa: ANN001
        # SConstruct working directory is the project root
        project_dir = env.subst("$PROJECT_DIR")
        print(f"\n[icons] Running icon pipeline (PlatformIO pre-build)...")
        run_pipeline(project_dir)

    env.AddPreAction("buildprog", _pio_pre_build)  # type: ignore  # noqa: F821

except NameError:
    # Not running inside PlatformIO — standalone mode
    pass


# ---------------------------------------------------------------------------
# Standalone entry point
# ---------------------------------------------------------------------------

def main():
    # When run directly, assume the script lives at the project root
    script_dir = os.path.dirname(os.path.abspath(__file__))
    run_pipeline(script_dir)


if __name__ == "__main__":
    main()
