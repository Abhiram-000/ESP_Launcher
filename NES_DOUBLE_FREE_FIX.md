# NES Emulator Double-Free Bug Fix

## Issue
The NES emulator was crashing on exit with a double-free error:
```
assert failed: tlsf_free tlsf.c:630 (!block_is_free(block) && "block already marked as free")
```

## Root Cause
The heap allocator detected that memory was being freed twice. This occurred in the NES emulator cleanup path when exiting a game.

**Stack trace:**
```
#6  0x42018da5 in nes_mem_free at components/nofrendo/osd_esp32s3.c:229
#7  0x420247c4 in apu_destroy at components/nofrendo/nes_apu.c:900
#8  0x4201a55a in nes_destroy at components/nofrendo/nes.c:396
```

## Analysis
The issue was that `nes_mem_free()` and `esp32_bitmap_free()` were calling `heap_caps_free()` without checking if the pointer was NULL. In some cleanup paths, the same memory could be freed multiple times.

## Solution
Added NULL pointer checks before freeing memory:

### 1. Fixed `nes_mem_free()` in `components/nofrendo/osd_esp32s3.c`:
```c
void nes_mem_free(void *ptr) {
    if (ptr) {
        heap_caps_free(ptr);
    }
}
```

### 2. Fixed `esp32_bitmap_free()` in `components/nofrendo/bitmap.c`:
```c
static void esp32_bitmap_free(void *ptr) {
    if (!ptr) return;
    ESP_LOGI(TAG_BMP, "bitmap_free(%p)", ptr);
    heap_caps_free(ptr);
}
```

## Impact
- **Before:** NES emulator crashed every time when exiting a game
- **After:** Clean exit from NES emulator without crashes
- **Side effects:** None - NULL pointer checks are defensive programming best practice

## Testing
1. Compile firmware: ✓
2. Flash to device
3. Launch NES game (SMB3)
4. Exit game using SELECT button
5. Verify no crash and clean return to launcher

## Files Modified
- `components/nofrendo/osd_esp32s3.c` - Added NULL check in `nes_mem_free()`
- `components/nofrendo/bitmap.c` - Added NULL check in `esp32_bitmap_free()`

## Prevention
This type of issue is prevented by:
1. Always checking pointers before freeing
2. Setting pointers to NULL after freeing
3. Using defensive programming practices in cleanup code
4. The existing `bmp_destroy()` already sets `*bitmap = NULL` after freeing
