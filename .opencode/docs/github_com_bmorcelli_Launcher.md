# GitHub - bmorcelli/Launcher: Firmware Launcher for ESP32 boards like: M5Stack, Lilygo, Marauder and CYD devices. · GitHub

> Source: https://github.com/bmorcelli/Launcher
> Cached: 2026-05-30T06:32:46.428Z

---

# Launcher

[](#launcher)
Application launcher for M5Stack, Lilygo, CYD, Marauder, and ESP32 devices.

    [](https://github.com/bmorcelli/Launcher/blob/main/M5Launcher.png?raw=true) [](https://github.com/bmorcelli/Launcher/blob/main/New Launcher.jpg?raw=true) [](https://github.com/bmorcelli/Launcher/blob/main/Launcher_anim.webp?raw=true)

Join our [Discord community](https://discord.gg/BE9by2a2FF)

## How to Install

[](#how-to-install)

- Use the Flasher: [Launcher Flasher](https://bmorcelli.github.io/Launcher/).

- Use M5Burner, or

- Download the .bin file from Releases for your device and use [https://web.esphome.io/](https://web.esphome.io/) or esptool.py to flash the file `Launcher-{YourDevice}.bin` to your device.

## How to Use

[](#how-to-use)

- Turn your device on

- Press M5/Sel (Enter) on the Launcher start screen to get into Launcher

- Choose OTA to install new binaries from online services (M5Burner or GitHub links)

- After installation, when you turn on the device, the installed program will launch if you don't press anything.

## My SD Card doesn't work!

[](#my-sd-card-doesnt-work)

- Make sure your SD card is SDHC (not SDXC)

- Use a maximum size of 32GB (I use 8 or 16GB)

- Format it as FAT32 (use [Rufus](https://rufus.ie/) to format your SD card)

- Ensure the partition scheme is MBR, not GPT

## With Launcher you'll be able to:

[](#with-launcher-youll-be-able-to)

  ### OTA - OTA Update

[](#ota---ota-update)

- Install binaries from online repositories (M5Burner or GitHub links) without needing a USB cable

- Install binaries from a WebUI that you can start from the WUI option, using binaries you have on your computer or smartphone

- Install binaries from your SD card

  ### SD - SD Card Management

[](#sd---sd-card-management)
- Create new folders,
- Delete files and folders,
- Rename files,
- Copy and paste files,
- Install binaries

  ### WUI - Web User Interface

[](#wui---web-user-interface)
- Manage files on the SD Card
- Install binaries wirelessly using the OTA Update option
- Edit text files
- Edit NVS information (UiFlow2 data, Launcher settings, and others)
- Deploy installations from the file list

  ### CFG - Configurations (Customization)

[](#cfg---configurations-customization)
- Charge Mode
- Change brightness
- Change dim time
- Change UI color
- Avoid/Ask SPIFFS (change whether Launcher asks to install the SPIFFS file system; only Orca One uses this feature)
- Change rotation
- All files/Only Bins (see all files or only .bins - default)
- Change partition scheme (allows installing large apps or UiFlow2, for example)
- List of partitions
- Clear the FAT partition
- Save SPIFFS (save a copy of the SPIFFS partition to restore when needed)
- Restore SPIFFS

  ### PMan - Partition Manager

[](#pman---partition-manager)
- View current partition scheme
- Create partitions
- Delete partitions
- Format partitions
- Back up data partitions (SPIFFS or FAT)
- Restore data partitions (SPIFFS or FAT)
- Resize partitions

  ### Tips

[](#tips)
* Having an SD card gives you a better experience, but it is not required. [SD Card Hat for M5StickCs]([https://www.thingiverse.com/thing:6459069](https://www.thingiverse.com/thing:6459069))
* You can learn more about how it works on the [Launcher Wiki]([https://github.com/bmorcelli/Launcher/wiki/Explaining-the-project](https://github.com/bmorcelli/Launcher/wiki/Explaining-the-project)).
* Where/how do I find binaries to launch? -> [Obtaining binaries to launch]([https://github.com/bmorcelli/Launcher/wiki/Obtaining-binaries-to-launch](https://github.com/bmorcelli/Launcher/wiki/Obtaining-binaries-to-launch))
* Now you can download binaries from [HERE!]([https://bmorcelli.github.io/Launcher/catalog.html](https://bmorcelli.github.io/Launcher/catalog.html))

## Known Issues

[](#known-issues)

- UiFlow 1 doesn't work with Launcher. It uses an old MicroPython distro, which uses an old ESP-IDF distro with lots of secrets that I couldn't figure out.

## To-Do list

[](#to-do-list)
Things that need to be done in future updates

-  LVGL UI (?)

-  Move to ESP-IDF Platform

-  Optimize Settings functions to save flash

-  Optimize Storage functions to save Flash

## Latest Changelog

[](#latest-changelog)

2.7.2:

-  Adjusted boot logic, now it works as it was before.

-  Fixed devices that share TFT and Touch SPI bus (NM-CYD-C5 and others), that were unable to install firmware from SD Card.

-  Fixed devices that share TFT with SD Card being unable to download firmware from OTA.

2.7.1:

-  Fixed Launcher auto updater process.

2.7.0:

-  Improved navigation on devices with 5 buttons and keyboards; now you can navigate up and down on the main menu

-  Improved main menu for small screens (T-Dongle, StickC)

-  Custom Partition Manager "PMan" (no more hardcoded partitions on Cardputer)

-  Automatic partition maker, firmware sanity checker, and flash optimizer.

-  Ability to install more than one firmware at a time

-  Encrypted WiFi passwords within config.conf and NVS

-  Remodeled WebUI, adding the ability to edit text files and NVS fields saved in memory

-  Changed items on CFG for better understanding

-  Optimized WiFi/WebUI functions to save Flash

-  Enabled OTA for CYD-3248S035C and CYD-3248S035R

-  Port to [M5-DinMeter](https://shop.m5stack.com/products/m5stack-din-meter-w-m5stamps3?ref=Pirata)

-  Port to [Arduino Nesso N1](https://docs.arduino.cc/hardware/nesso-n1)

-  Port to [Reaper Board](https://www.elecrow.com/bruce-pcb-rf-reaper.html)

  ## Older Changelogs

[](#older-changelogs)

2.6.10:

-  Fixed navigation on 3-button devices that were triggering EscPress when pressing Next or Prev buttons.

2.6.9:

-  Port to [NM-CYD-C5](https://www.nmminer.com/product/nm-cyd-c5/)

-  Port to [3248W535C](https://www.displaysmodule.com/sale-51335661-jc3248w535c-i-y-lcd-display-module-with-320-480-pixel-resolution-and-effective-display-area-73-4-49-.html)

-  Port to [Waveshare-ESP32-S3-LCD-1.47](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47)

-  Port to [T-Dongle-C5 (tft)](https://lilygo.cc/products/t-dongle-c5?bg_ref=sDI8Bh4HmO)

-  Port to [T-Display S3 AMOLED Plus](https://lilygo.cc/products/t-display-s3-amoled-plus)

-  Fix issue where brightness wasn't being saved

-  Fix issue where custom "downloads" folder wasn't being used

-  Fix issue where some firmware downloads weren't working due to prohibited characters in firmware names

-  Fixed SD card not mounting on T-Watch Ultra and adjusted UI for rounded edges on the main menu and file listings

-  Enabled WASD navigation for Lilygo T-LoraPager, T-Deck, T-Deck Plus, and T-Deck Pro

-  Added touch calibration for XPT2046-driven devices (resistive touchscreens, like CYD, Marauder, Phantom, and T-HMI). It will be prompted when no calibration data is found (first boot), or can be started from `CFG > Calibrate Touch` or by sending "calibrate" over Serial.

-  Added an "OFF" option to the main screen to turn off the device (DeepSleep when PMIC is not available).

-  Centralized main libraries using git submodules, so it doesn't download many copies of the same library to the `.pio/libdeps/**/` folder

-  Speed up workflow using GitHub cache for framework and libs

2.6.8:

-  Increased Game Station partition sizes. [#299](https://github.com/bmorcelli/Launcher/issues/299)

-  Added an option to erase the app partition

-  Added an option to always boot into Launcher [#292](https://github.com/bmorcelli/Launcher/issues/292)

-  Added "APP" menu item to reboot into the last installed firmware

-  Enhanced NVS partition and partition schemes for UiFlow2

-  Fix M5Stack Tab5 WiFi scan and WebUI in AP mode.

-  New Device: [Lilygo T-Watch-S3](https://lilygo.cc/products/t-watch-s3?bg_ref=sDI8Bh4HmO)

-  New Device: [Lilygo T-Watch-Ultra](https://lilygo.cc/products/t-watch-ultra?bg_ref=sDI8Bh4HmO)

-  New Device: [Lilygo E-Paper S3 Pro H752-x](https://lilygo.cc/products/t5-e-paper-s3-pro?bg_ref=sDI8Bh4HmO)

2.6.7:

-  Re-established Tab5 SD card on SPI for WiFi compatibility and refactored the reboot process to power cycle the SD card, resetting the SD card communication bus.

-  Tab5 now has a Mass Storage interface!

-  Fixed M5Stack Paper S3 USB Mass Storage issue [#287](https://github.com/bmorcelli/Launcher/issues/287) and OTA will show only S3 firmware.

-  Added app offset parameter to allow Launcher to be updated in Tab5, and firmware that is placed at different factory/app0 addresses.

-  Set StickC and CPlus devices to see only ESP32 firmware on OTA, excluding ESP32-S3 (StickS3) firmware

-  Adjusted firmware binaries that are not merged to have a partition table, allowing a SPIFFS partition to be attached to them. This binary will be valid ONLY for Launcher; it can't be flashed alone because it doesn't have a valid bootloader and partition table. [#289](https://github.com/bmorcelli/Launcher/issues/289)

2.6.6:

-  [M5-StickS3](https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit?ref=Pirata) port

-  (Beta) [M5-DinMeter](https://shop.m5stack.com/products/m5stack-din-meter-w-m5stamps3?ref=Pirata) port

-  [Arduino Nesso N1](https://docs.arduino.cc/hardware/nesso-n1) port

2.6.5:

-  M5-Tab5 using SDMMC driver for compatibility with other firmware

-  Added the ability to order by "Latest update"

-  Port to OpenSourceSRDLabs [WaveSentry and WaveSentry Pro ](https://opensourcesdrlab.com/products/aifw-wavesentry-esp32?VariantsId=10331)

-  Battery ADC measurement fix for Cardputer, Tdeck, StickCPlus2, T-Display S3, T-HMI

2.6.4:

-  Fixed CYD 3243S035R touchscreen rotation

-  Fixed Marauder V7 screen issues

-  Enhanced M5-PaperS3 display, using Sprites now for better drawing resolution

-  Fixed M5Stack Cardputer ADV keyboard not adding '*' and '('

-  Add new partition scheme for [Cardputer Game Station](https://github.com/geo-tp/Cardputer-Game-Station-Emulators/), allowing 4.5MB game ROMs

-  Port to [M5-Paper](https://shop.m5stack.com/products/m5paper-esp32-development-kit-v1-1-960x540-4-7-eink-display-235-ppi?ref=Pirata)

-  Port to [Arduino Nesso N1](https://docs.arduino.cc/hardware/nesso-n1), not building yet

2.6.3:

-  Fixed Marauder keyboard

-  Changed from EEPROM to NVS to save configs and WiFi credentials [Issue 232](https://github.com/bmorcelli/Launcher/issues/232)

-  Port to [M5-PaperS3](https://shop.m5stack.com/products/m5papers3-esp32s3-development-kit?ref=Pirata)

-  Fixed SelPress leaking into main menu

-  T-Embed (all) and T-LoraPager Encoder enhancement

-  WebUi session enhancement and fixes

2.6.2:

-  Fixed T-Embed CC1101 OTA Link

-  Changed SD file listings for speed([PR 230](https://github.com/bmorcelli/Launcher/pull/230)) [Issue 229](https://github.com/bmorcelli/Launcher/issues/229) thanks @geo-tp and @emericklaw

2.6.1:

-  Fixed T-Deck Plus touchscreen (added new env for it)

-  Fixed M5Stack CoreS3 SD Card not mounting

-  Add Delete from Favorites option

2.6.0:

-  Pulling data from my new API, integrating all device lists with the M5Burner API, and counting downloads in the M5Burner database.

-  OTA Pagination (pages of 100 firmware ordered by download-default)

-  New Device: [M5Stack Tab5](https://shop.m5stack.com/products/m5stack-tab5-iot-development-kit-esp32-p4?ref=Pirata).

-  New Device: [Lilygo T-HMI](https://www.lilygo.cc/products/t-hmi?bg_ref=sDI8Bh4HmO)

-  New Feature: Backup SPIFFS/FAT now prompts to merge data into a chosen binary, so you can install firmware and data by choosing `SPIFFS Yes` during install, and backup binaries now have incremental names.

-  New Feature: Added filter and ordering to the firmware list.

-  New Feature: Added a *Starred* firmware list (controlled by me; support the project to have your firmware added to the starred list.).

-  New Feature: Added "Favorite" (requires SD card), where you can add firmware from the OTA list or manually add binary links from your local server or elsewhere.

-  config.conf changes:

```
...
    "favorite": [
      { // Example of firmware added into Favorites through OTA function
        "name": "Evil-Cardputer-7h30th3r0n3",
        "fid": "2128851a0c98a4c1d15ac1a327b49812",
        "link": ""
      },
      { // Example of my custom link file added by manually editing this file
        "name": "Launcher Beta link",
        "fid": "", // leave it blank
        "link": "https://github.com/bmorcelli/Launcher/releases/download/beta/Launcher-m5stack-cardputer.bin"
      },
      {
        "name": "Bruce Beta link",
        "fid": "",
        "link": "https://github.com/pr3y/Bruce/releases/download/betaRelease/Bruce-m5stack-cardputer.bin"
      },
    ],
    "c0:4e:30:13:8d:f4": 1, // Rotation is now bound to hardware MAC
...

```

2.5.3:

-  Restored T-Deck OTA

-  Refined T-Deck Touchscreen inputs

2.5.2:

-  Fixed Marauder V6 touchscreen and CYDs touchscreen unresponsive [issue](https://github.com/bmorcelli/Launcher/issues/210) and fixed Dim screen

-  Fixed Marauder Mini and V7 screen dimming.

-  Fixed Smoochiee board inputs and SD card [issue](https://github.com/bmorcelli/Launcher/issues/209)

-  Fixed Longpress on Cardputer ADV

-  Fixed folder creation on subfolders

2.5.1:

-  Fixed Cardputer ADV Keyboard compatibility

-  Fixed issue where OTA firmware list wasn't being fully downloaded.

2.5.0:

 Moving to pioarduino 3.3 based framework (ESP-IDF 5.5)

-  Enable USB Mass Storage to SD_MMC devices (T-Display-S3 and touch and T-Dongle S3 tft)

-  Partition changes

-  Firmware updates

-  OTA lists and installation

-  Added keyboard support to T-Deck Pro [#180](https://github.com/bmorcelli/Launcher/issues/180)

-  Fixed compatibility with UIFlow 2.3.x [#192](https://github.com/bmorcelli/Launcher/issues/192)

-  Added CSS, JS, Html online minifier

-  Moved to ESP32Async/ESPAsyncWebServer official repo

-  Port to [Cardputer ADV](https://shop.m5stack.com/products/m5stack-cardputer-adv-version-esp32-s3?ref=Pirata) thanks to [@n0xa](https://github.com/n0xa)

-  Port to [Lilygo Lora Pager](https://lilygo.cc/products/t-lora-pager) by @emericklaw

-  Port to [CYD-4827S043R](https://github.com/bmorcelli/Launcher/issues/186) -> WIP

-  Fixed first line [filelist](https://github.com/bmorcelli/Launcher/issues/166)

2.4.10:

-  Fixed T-Embed screen

-  Fixed StickC (and plus) keyboard navigation

-  Phantom touchscreen mapping

2.4.9:

-  Fixed T-Display-S3 PRO

-  Enabled OTA for Marauder Mini

2.4.8:

-  Enabled OTA function to: CYD 2432S028R, 2-USB, S024R, W328C/R, Marauder boards, Awok boards, Phantom, Lilygo T-Embed CC1101 and T-Deck (regular and plus)

-  Port to [AWOK Mini v2](https://awokdynamics.com/products/dual-mini-v2) and [AWOK Touch v2](https://awokdynamics.com/products/dual-touch-v2)

-  Port to [RabbitLabs Phantom](https://rabbit-labs.com/product/the-phantom-by-rabbit-labs/?v=dc634e207282)

-  Ports to [8048S043C, 8048W550C](https://github.com/bmorcelli/Launcher/issues/108)

-  Port to Lilygo T-Deck Pro (e-paper display)

-  Enhancements on Touchscreen device

... [Content truncated]