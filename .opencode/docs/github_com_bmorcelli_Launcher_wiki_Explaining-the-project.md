# Explaining the project · bmorcelli/Launcher Wiki · GitHub

> Source: https://github.com/bmorcelli/Launcher/wiki/Explaining-the-project
> Cached: 2026-05-30T06:33:40.354Z

---

bmorcelli
    
    /
    
      [Launcher](/bmorcelli/Launcher)
    

    Public
  

        

        
            

    
        
          
  
  
    
  
    
      

              Uh oh!

              There was an error while loading. [Please reload this page]().

  
  

        

      

  
                
    
Notifications
    You must be signed in to change notification settings

  

  
              
    
Fork
    194

  

  
        
            
    

          Star
          1.6k

  

        
      

        

          

  

      
  
    
              
    

        Code
          

    

      
  
    
              
    

        Issues
          19

    

      
  
    
              
    

        Pull requests
          0

    

      
  
    
              
    

        Actions
          

    

      
  
    
              
    

        Projects
          

    

      
  
    
              
    

        Wiki
          

    

      
  
    
              
    

        Security and quality
          0

    

      
  
    
              
    

        Insights
          

    

          
  
      
    

Additional navigation options

  
    
                
  
    

        
    
    
    
        
          
    

        
      
        
          Code
      

  

        
    
    
    
        
          
    

        
      
        
          Issues
      

  

        
    
    
    
        
          
    

        
      
        
          Pull requests
      

  

        
    
    
    
        
          
    

        
      
        
          Actions
      

  

        
    
    
    
        
          
    

        
      
        
          Projects
      

  

        
    
    
    
        
          
    

        
      
        
          Wiki
      

  

        
    
    
    
        
          
    

        
      
        
          Security and quality
      

  

        
    
    
    
        
          
    

        
      
        
          Insights
      

  

    

      
  

  
  

    
    

    
      
  
    
      # Explaining the project

        
          [Jump to bottom](#wiki-pages-box)

        
    

      
        Pirata edited this page May 24, 2026
        &middot;
        [6 revisions](/bmorcelli/Launcher/wiki/Explaining-the-project/_history)
      

    
      
            
              
                # How Launcher works

[](#how-launcher-works)
Launcher is a small resident firmware that manages other firmware images on ESP32 devices. It is not only an app picker. It also understands firmware layouts, partition tables, SD card storage, WebUI uploads, online firmware metadata, and device-specific input/display differences.

The current project is built with PlatformIO, Arduino framework, and the pioarduino ESP32 platform based on ESP-IDF 5.5 libraries. Board support lives under `boards/`, shared logic lives under `src/`, and common libraries are centralized in `lib_modules/`.

## Boot flow

[](#boot-flow)
Launcher is installed in its own protected app partition. When the device powers on, Launcher shows a start screen. If you press the select/enter button, it opens the Launcher menu. If you do nothing and `Boot to Launcher` is disabled, Launcher starts the selected installed app.

When more than one app is installed, Launcher can show the installed apps as menu items. Selecting one opens actions to launch, rename, or delete that app.

## Main parts

[](#main-parts)
Launcher is split into these major areas:

- Boot and app registry: tracks installed OTA app partitions, stores display names in NVS, and selects which app should boot.

- Installer: writes app images, SPIFFS/LittleFS data, and FAT data to the right partitions.

- Partition manager: reads, validates, edits, compacts, backs up, restores, and writes ESP32 partition tables.

- SD manager: browses SD files and installs local `.bin` files.

- Online installer: queries LauncherHub firmware metadata and downloads firmware from the online service.

- WebUI: exposes file management, OTA upload, text editing, and NVS editing through a browser.

- Board layer: each supported device has its own PlatformIO environment, board JSON, pinout header, and `interface.cpp` setup code.

## Firmware formats

[](#firmware-formats)
Launcher can install normal application binaries and merged firmware binaries.

A normal application binary is the `.bin` produced by Arduino, PlatformIO, or ESP-IDF for the app itself. It usually starts with the ESP image magic byte `0xE9` and is written to an OTA app partition.

A merged binary may include bootloader, partition table, app image, and sometimes data partitions in one file. Launcher inspects the binary, reads partition metadata when available, extracts the application section, and can install associated data partitions when the firmware package declares or contains them.

Full flash dumps are larger images that may contain unused or unknown bytes. Launcher does not blindly write an entire dump over its own flash layout. It identifies useful regions, checks app image validity, and writes only what fits into the selected install layout.

## Dynamic partitions

[](#dynamic-partitions)
Older Launcher versions depended mostly on fixed partition schemes. Current Launcher uses a dynamic partition model.

The partition manager can:

- list app and data partitions

- show details for each partition

- create OTA app, FAT, and SPIFFS partitions in free space

- resize or remove editable partitions

- format editable data partitions

- back up and restore FAT or SPIFFS data partitions

- compact the table and migrate moved data when needed

- apply or discard pending changes

Protected system partitions are kept safe. Launcher validates alignment, offsets, sizes, OTA metadata, and required system entries before writing a generated partition table.

## Installing apps

[](#installing-apps)
When installing a firmware, Launcher chooses an install layout based on the app size and any required data partitions. It can use an existing OTA slot, create a new app partition, resize free space, or offer repartition choices when the current layout is not large enough.

For firmware with data files, Launcher can install:

- the application image into an OTA app partition

- SPIFFS or LittleFS-style data into the `spiffs` partition

- FAT data into `vfs` or `sys` partitions, used by firmware such as UIFlow2, CircuitPython, and MicroHydra-style environments

After installation, Launcher stores app metadata and can expose the installed firmware as a launcher menu entry.

## Configuration storage

[](#configuration-storage)
Launcher stores current settings in NVS and can also keep SD-card configuration data in `/config.conf` for compatibility and portability. WiFi credentials are encrypted before being saved. WebUI credentials, UI colors, dim time, orientation, file listing behavior, boot behavior, and the last installed app are all persisted.

The default WebUI login is:

user: `admin`

password: `launcher`

Change these from the configuration file or NVS editor if the device is used on an untrusted network.

## Supported device model

[](#supported-device-model)
Launcher supports many hardware families, not a single fixed board. Each device port usually includes:

`boards/_jsonfiles/[board].json` for the PlatformIO board definition

`boards/[board]/platformio.ini` for the build environment

`boards/[board]/interface.cpp` for board-specific setup

`boards/pinouts/[board].h` for display, SD, buttons, touch, keyboard, battery, and bus pins

The repository currently includes targets for M5Stack devices, LilyGo devices, CYD displays, Marauder/AWOK-style boards, Elecrow display boards, headless ESP32/ESP32-S3 builds, Arduino Nesso N1, Reaper, Smoochiee, Waveshare, and other ESP32 display modules.

              

          

            
            
              

  
    
        
    

Toggle table of contents

      Pages 4
    
  
  
    
      
    

    

        
          
  
      
    
  
    
    
    Loading

        
    

  

    
        Home
        

  
      
  

  
    
  
    
      

              Uh oh!

              There was an error while loading. [Please reload this page]().

  
  
  

        
        
          
  
      
    
  
    
    
    Loading

        
    

  

    
        Explaining the project
        

  
        

      
        [How Launcher works](/bmorcelli/Launcher/wiki/Explaining-the-project#how-launcher-works)
      
      
        [Boot flow](/bmorcelli/Launcher/wiki/Explaining-the-project#boot-flow)
      
      
        [Main parts](/bmorcelli/Launcher/wiki/Explaining-the-project#main-parts)
      
      
        [Firmware formats](/bmorcelli/Launcher/wiki/Explaining-the-project#firmware-formats)
      
      
        [Dynamic partitions](/bmorcelli/Launcher/wiki/Explaining-the-project#dynamic-partitions)
      
      
        [Installing apps](/bmorcelli/Launcher/wiki/Explaining-the-project#installing-apps)
      
      
        [Configuration storage](/bmorcelli/Launcher/wiki/Explaining-the-project#configuration-storage)
      
      
        [Supported device model](/bmorcelli/Launcher/wiki/Explaining-the-project#supported-device-model)
      
  

  

        
        
          
  
      
    
  
    
    
    Loading

        
    

  

    
        Functionalities explained
        

  
      
  

  
    
  
    
      

              Uh oh!

              There was an error while loading. [Please reload this page]().

  
  
  

        
        
          
  
      
    
  
    
    
    Loading

        
    

  

    
        Obtaining binaries to launch
        

  
      
  

  
    
  
    
      

              Uh oh!

              There was an error while loading. [Please reload this page]().

  
  
  

        
    

  

            

            ### Clone this wiki locally