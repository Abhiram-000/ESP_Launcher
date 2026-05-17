# 🎮 NES ROM Quick Reference

## ✅ READY TO PLAY (4 games)

| Game | Mapper | Size | Status |
|------|--------|------|--------|
| **Contra Force** | 4 (MMC3) | 257 KB | ✓ PLAYABLE |
| **Mega Man 2** | 1 (MMC1) | 257 KB | ✓ PLAYABLE |
| **Super Mario Bros. 3** | 4 (MMC3) | 385 KB | ✓ PLAYABLE |
| **Super Mario Bros. 3 (USA)** | 4 (MMC3) | 385 KB | ✓ PLAYABLE |

## ✗ NOT SUPPORTED (2 games)

| Game | Mapper | Reason |
|------|--------|--------|
| **Multipack** | 227 | Mapper not implemented |
| **Pacmania** | 206 | Mapper not implemented |

## 🎯 Controls

- **D-Pad:** Move
- **A Button:** Jump/Action
- **B Button:** Run/Fire
- **Start:** Pause
- **SELECT:** Exit to launcher

## 🔧 Quick Commands

```bash
# Flash firmware
pio run -t upload -e esp32-s3-devkitc-1

# Flash ROMs
pio run -t uploadfs -e esp32-s3-devkitc-1

# Monitor serial
pio device monitor -e esp32-s3-devkitc-1

# Verify ROMs
./verify_roms.sh
```

## 📝 What Was Fixed

1. ✅ Double-free crash when exiting games
2. ✅ Mapper support list corrected (40 mappers)
3. ✅ All 6 ROMs verified and flashed
4. ✅ Catalog updated with playability status

## 🎉 Status: COMPLETE & TESTED

Your device is ready! Launch any of the 4 playable games and enjoy! 🚀
