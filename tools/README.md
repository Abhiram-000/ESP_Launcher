# CipherOS Store Manager

The Store Manager is a desktop GUI tool that automates the management of your CipherOS Game Store and HackStore catalogs.

## Prerequisites
- Python 3
- Git (must be installed and authenticated to push to your GitHub)

## How to use

1. **Create your Repository (One-time setup):**
   Create a folder on your computer for your store, initialize it with Git, and link it to your GitHub repository.
   ```bash
   mkdir CipherOS_Store
   cd CipherOS_Store
   git init
   git remote add origin https://github.com/YourUsername/CipherOS_Store.git
   ```

2. **Launch the Tool:**
   From the root of the `Custom_Launcher-main` project, run:
   ```bash
   python3 tools/store_manager.py
   ```

3. **Load the Repository:**
   In the GUI, click the **Browse** button at the top and select the `CipherOS_Store` folder you created in Step 1. Click **Load**.
   
   *(Note: You do NOT enter your GitHub link manually into the GUI. The tool automatically reads your GitHub link from the folder's hidden `.git/config` file!)*

4. **Add a Binary / Game:**
   - Go to the **NES Game Store** or **HackStore Firmware** tab.
   - Click **Browse** under the file section to select a `.nes` or `.bin` file.
   - The tool will automatically calculate the file size, CRC32 checksum, and auto-generate the raw GitHub URL based on your repo's remote origin.
   - Fill in any remaining details (Name, Version, Description).
   - Click **Save**. The tool will automatically copy the file into the `roms/` or `firmwares/` folder and update the JSON catalog.

5. **Upload to Cloud:**
   Click the green **Commit & Push** button. The tool will automatically stage the changes, commit them, and push them to your GitHub repository. 
   *(Note: Ensure your terminal has the proper Git credentials cached, otherwise the push may fail).*
