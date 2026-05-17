# Game Store Scrolling Guide

## Current Situation

You have **6 games total** in your catalog:
- **4 playable** (Contra Force, Mega Man 2, SMB3, SMB3 USA)
- **2 unsupported** (Multipack, Pacmania)

## Why You Can't Scroll in "MY GAMES"

The **"MY GAMES"** screen only shows **installed** games. Since you have 4 installed games and the screen can display 4 rows at once, there's nothing to scroll!

**Screen capacity:** 4 rows visible at once
**Installed games:** 4 games
**Result:** All games fit on one screen = No scrolling needed

## How to See All 6 Games with Scrolling

### Option 1: Use the GAME STORE View
1. From "MY GAMES" screen, press **RIGHT** button
2. You'll switch to "GAME STORE" view
3. This shows **all 6 games** (installed + not installed)
4. Now you can scroll:
   - Press **DOWN** to go through items 1, 2, 3, 4
   - Keep pressing **DOWN** on item 4 → screen scrolls to show items 2, 3, 4, 5
   - Keep pressing **DOWN** → screen scrolls to show items 3, 4, 5, 6

### Option 2: Add More Games
If you add more than 4 playable games, "MY GAMES" will scroll automatically.

## Controls Reference

### MY GAMES Screen
```
UP/DOWN    - Navigate through installed games
SELECT     - Launch selected game
RIGHT      - Switch to GAME STORE (shows all games)
LEFT       - Back to app grid
```

### GAME STORE Screen
```
UP/DOWN    - Navigate with auto-scroll (when >4 games)
SELECT     - View game details
LEFT       - Back to MY GAMES
```

### Game Detail Screen
```
SELECT     - Play (if installed) or Install (if not)
DOWN       - Delete game (if installed)
LEFT       - Back to previous list
```

## Scrolling Behavior

The scrolling works like this:

**Visible rows:** 4 (positions 1, 2, 3, 4 on screen)

| Your Position | What You See | Scroll Offset |
|---------------|--------------|---------------|
| Item 1 | Items 1,2,3,4 | 0 |
| Item 2 | Items 1,2,3,4 | 0 |
| Item 3 | Items 1,2,3,4 | 0 |
| Item 4 | Items 1,2,3,4 | 0 |
| Item 5 | Items 2,3,4,5 | 1 (scrolled) |
| Item 6 | Items 3,4,5,6 | 2 (scrolled) |

## Testing Scrolling

1. **Flash the device** (if you haven't already):
   ```bash
   pio run -t upload -e esp32-s3-devkitc-1
   pio run -t uploadfs -e esp32-s3-devkitc-1
   ```

2. **Navigate to Game Store:**
   - From launcher, select "Game Store" app
   - You'll see "MY GAMES" with 4 installed games

3. **Switch to Store view:**
   - Press **RIGHT** button
   - Screen changes to "GAME STORE" header
   - You should see all 6 games

4. **Test scrolling:**
   - Press **DOWN** repeatedly
   - After the 4th item, the list should scroll
   - You should see Multipack and Pacmania appear

## Troubleshooting

### "I only see 2 games in MY GAMES"
- This means only 2 games are actually installed on the device
- Flash the filesystem: `pio run -t uploadfs -e esp32-s3-devkitc-1`

### "RIGHT button doesn't work"
- Make sure you're connected to Wi-Fi first
- If no Wi-Fi, you'll see "NO WI-FI" error
- Go to Settings → Wi-Fi to connect

### "I see all 6 games but can't scroll"
- This is expected if you're on "MY GAMES" with only 4 installed
- Press RIGHT to go to "GAME STORE" view
- Scrolling works there with all 6 games

### "Scrolling is too fast/slow"
- Each button press moves one item
- Scroll offset updates automatically when needed
- This is normal behavior

## Visual Indicator

When scrolling is active, you'll notice:
- Selected item (highlighted in accent color) moves down
- When it reaches the 4th row, the entire list shifts up
- Top items disappear, bottom items appear
- This creates a "window" effect over your game list

## Summary

**To scroll through all 6 games:**
1. Open Game Store app
2. Press **RIGHT** to switch to "GAME STORE" view
3. Use **UP/DOWN** to navigate and scroll
4. Press **SELECT** to view game details

The scrolling is working correctly - you just need to be in the right view (GAME STORE) to see all games!
