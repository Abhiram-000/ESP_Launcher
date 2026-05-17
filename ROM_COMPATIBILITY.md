# NES ROM Compatibility Status

## Overview
Your ESP32-S3 Custom Launcher has 6 NES ROMs installed. The Nofrendo emulator supports specific mappers only.

## ROM Status Summary
- **4 ROMs are PLAYABLE** ✓
- **2 ROMs are NOT SUPPORTED** ✗ (missing mapper implementation)

---

## ✓ PLAYABLE ROMS (4)

### 1. Contra Force
- **File:** `contra_force.nes` (257 KB)
- **Mapper:** 4 (MMC3)
- **Status:** ✓ FULLY SUPPORTED
- **Genre:** Action

### 2. Mega Man 2
- **File:** `megaman2.nes` (257 KB)
- **Mapper:** 1 (MMC1)
- **Status:** ✓ FULLY SUPPORTED
- **Genre:** Action/Platform

### 3. Super Mario Bros. 3
- **File:** `smb3.nes` (385 KB)
- **Mapper:** 4 (MMC3)
- **Status:** ✓ FULLY SUPPORTED
- **Genre:** Platform

### 4. Super Mario Bros. 3 (USA)
- **File:** `smb3_usa.nes` (385 KB)
- **Mapper:** 4 (MMC3)
- **Status:** ✓ FULLY SUPPORTED
- **Genre:** Platform
- **Note:** Duplicate of smb3.nes

---

## ✗ NOT SUPPORTED ROMS (2)

### 1. Multipack
- **File:** `multipack.nes` (513 KB)
- **Mapper:** 227
- **Status:** ✗ NOT SUPPORTED
- **Reason:** Mapper 227 not implemented in Nofrendo
- **Action:** Will show "Unsupported mapper" error if launched

### 2. Pacmania
- **File:** `pacmania.nes` (97 KB)
- **Mapper:** 206 (Namco 118 / Tengen MIMIC-1)
- **Status:** ✗ NOT SUPPORTED
- **Reason:** Mapper 206 not implemented in Nofrendo
- **Action:** Will show "Unsupported mapper" error if launched

---

## Supported Mappers (Complete List)

The Nofrendo emulator in this build supports these mappers:

```
0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11,
15, 16, 18, 19, 24,
32, 33, 34, 40, 41, 42, 46, 50,
64, 65, 66, 70, 73, 75, 78, 79,
85, 87, 93, 94, 99,
160, 229, 231
```

**Most common mappers:**
- Mapper 0 (NROM) - Simple games
- Mapper 1 (MMC1) - Mega Man 2, Zelda, etc.
- Mapper 2 (UxROM) - Mega Man, Castlevania
- Mapper 3 (CNROM) - Simple games
- Mapper 4 (MMC3) - Super Mario Bros. 3, most advanced NES games

---

## How to Add More ROMs

1. **Check mapper compatibility:**
   - Use a ROM info tool to check the mapper number
   - Verify it's in the supported list above

2. **Add ROM file:**
   - Place `.nes` file in `data/roms/` directory
   - Keep filename lowercase with underscores (e.g., `game_name.nes`)

3. **Update catalog:**
   - Edit `data/catalog.json`
   - Add entry with correct mapper number and file size

4. **Flash to device:**
   ```bash
   pio run -t uploadfs -e esp32-s3-devkitc-1
   ```

---

## Testing Instructions

### Flash the firmware and filesystem:
```bash
# Flash firmware
pio run -t upload -e esp32-s3-devkitc-1

# Flash filesystem (ROMs + catalog)
pio run -t uploadfs -e esp32-s3-devkitc-1
```

### Test each playable ROM:
1. Navigate to Game Store in launcher
2. Select a game (Contra Force, Mega Man 2, or SMB3)
3. Press SELECT to launch
4. Verify game loads and runs
5. Press SELECT to exit back to launcher
6. Verify no crash (double-free bug is fixed)

### Expected behavior for unsupported ROMs:
- Multipack and Pacmania will show error: "ROM mapper XXX not supported"
- Launcher will remain stable and return to game list

---

## Fixes Applied

### 1. Double-Free Bug Fix
- Added NULL checks in `nes_mem_free()` and `esp32_bitmap_free()`
- Prevents crash when exiting NES emulator
- Files modified:
  - `components/nofrendo/osd_esp32s3.c`
  - `components/nofrendo/bitmap.c`

### 2. Mapper List Correction
- Updated `s_supported_mappers[]` in `src/ui/apps/App_NES.cpp`
- Now matches actual mapper implementations in `components/nofrendo/map*.c`
- Added missing mappers: 10, 41, 42, 46, 50, 73, 87, 93, 160, 229

### 3. Catalog Update
- Updated `data/catalog.json` with playability status
- Added genre information
- Marked unsupported ROMs clearly

---

## Recommendations

1. **Remove duplicate:** `smb3_usa.nes` is identical to `smb3.nes` - you can remove one to save space

2. **Replace unsupported ROMs:** Find alternative versions of Multipack and Pacmania that use supported mappers, or find different games

3. **Test thoroughly:** Test all 4 playable ROMs to ensure they work correctly with the double-free fix

4. **Monitor memory:** Watch for any memory issues during extended gameplay sessions
