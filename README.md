# Project Setup and Toolchain Installation Guide

## Building and Running:

> **Note:** If you're using a factory-new devboard that you've just unboxed, please follow [these instructions](#setting-up-a-brand-new-metro-m4-grand-central-devboard) first before continuing. If you've received a working devboard from another team member, you can continue.

1. **Start the SEGGER GDB Server:**

   - run `JLinkGDBServer` from the SEGGER folder containing all the J-Link tools.
   - Before clicking 'OK', make sure the target is set to `SAMD51P20A`, and the interface is set to `SWD`
   - WINDOWS/WSL ONLY: The "Localhost Only" checkbox must be unchecked.
   - Ensure the J-Link server is on port 2331 for GDB connections.

2. **Build the Project:**

   - Execute `make` to build the project.

3. **Connect and Run:**
   - Use `make connect` to connect to the board and auto-flash/run the program.
   - The code will automatically pause at the top of the 'main' function. Set any breakpoints you need, and then continue running the program with 'c'
   - To see output from printf statements, connect to localhost:19021 using Telnet. (You can use PuTTY for this on Windows, or the built-in terminal command `telnet` on Mac/Linux)


## Toolchain Installation

### All Platforms (Initial Step)

- Download Segger's J-Link tools from [here](https://www.segger.com/downloads/jlink/).

### Windows

1. Install Windows Subsystem for Linux (WSL):

   - Run `wsl --install` in PowerShell (as Administrator).
   - Follow prompts and restart your computer as required.

2. Clone the repository into the WSL filesystem. This is important for performance during compilation.

3. Install ARM toolchain for Linux:

   - `sudo apt install gcc-arm-none-eabi`

4. Install GDB Multiarch and other build tools:
   - `sudo apt install gdb-multiarch`
   - `sudo apt install build-essential`

### Mac/Linux (Geared towards Mac)

> **Note:** Skip to step 5 if you have a Mac with an Intel processor.

1. Edit the `~/.zshrc` file:

   - You can use `nano ~/.zshrc` to edit this file. Use `CTRL`+`X`, then `Y`, then `Enter` to quit and save.
   - Add these lines to the bottom of the file:
     ```bash
     alias arm="env /usr/bin/arch -arm64 /bin/zsh --login"
     alias intel="env /usr/bin/arch -x86_64 /bin/zsh --login"
     ```

2. Run `source ~/.zshrc`.

   - This should enable the `arm` and `intel` commands in your terminal. Test this out by running `intel` and checking that the result of running `arch` is `i386`. Then run `arm` and check that the result of `arch` is `arm64`.

3. Switch into an intel terminal by running the `intel` command you just created, and verify that the `arch` command returns `i386`

4. Install brew in the intel terminal by running the script at https://brew.sh/ and following the prompts

   - > **Note:** After the Brew installation is complete, it will prompt you to run two other commands. Remember to copy/paste them into the terminal and run these as well.

5. Install gdb:

   - `brew install gdb`

6. Download Arm Developer Tools:

   - Download & Install the .pkg from [here](<https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads#:~:text=macOS%20(Apple%20silicon)%20hosted%20cross%20toolchains>).
   - Make sure you're downloading for the right hardware.

7. Add the Arm Developer Tools to your path by adding the following line to the bottom of the `~/.zshrc` (or `~/.bash_profile`) file, similar to step 1

   - Add:
     ```bash
     export PATH="/Applications/ArmGNUToolchain/<VersionNumber>/arm-none-eabi/bin/:$PATH"
     ```
   - IMPORTANT: Remember to replace `<VersionNumber>` with the version number of the toolchain you downloaded. It should be something like '13.2.Rel1'

8. If you're on Mac, install GNU `sed` by running `brew install gnu-sed`

---

## Setting Up a Brand New Metro M4 Grand Central Devboard

The Grand Central runs on an Atmel SAMD51P20A, which has a `BOOTPROT` fuse protecting the flash area of the bootloader. If you have a factory-new board, you will need to clear the `BOOTPROT` fuse to allow your J-Link to flash code onto the board.

1. Install and open [Microchip Studio](https://www.microchip.com/en-us/tools-resources/develop/microchip-studio).
   > **Note:** Microchip Studio only supports Windows, so you might have to use a friend's computer for this, or in the worst case, install a Windows VM.
2. Connect your M4 Grand Central and J-Link to your computer via USB (and make sure they're connected to each other).
3. In Microchip Studio, go to `Tools > Device Programming`.
   - In the dialog that opens, select `Tool > J-Link`, `Device > ATSAMD51P20`, and `Interface > SWD`. Click `Apply`.
   - Click `Read`. The empty fields for _'Device Signature'_ and _'Target Voltage'_ will populate.
4. In the same 'Device Programming' window, select `Fuses`.
   - Change the value of `USER_WORD_0.NVMCTRL_BOOTPROT` to `0x0F` or `0 kbytes` (whichever shows up).
   - At the bottom of the window, click `Program` and then `Verify` to make sure the fuse has been set correctly.

That should be it. The J-Link GDB Server should now work as expected. Congrats on setting up your brand-new devboard!

---

## Adding a New File to the Project

> **Note:** This does not apply to .h files (although, you should still follow step 1)

1. Take 30 full seconds to think about the scope and name of this file:

   - Does the name encompass everything the file _could end up_ doing?
   - Is there a distinct logical separation between the role of this file and any other files?
   - Does it follow existing naming conventions (within the folder, and within the project)?
   - Does it belong in the folder you are adding it to? Will the file _always_ be doing things within this category?

2. Modify the `Makefile` to add the new file to the list of objects to be compiled:

   - Add the file's name to the OBJS list, with the .o extension instead of .c
   - If the file is in a new folder, add the newly created folder to the EXTRA_VPATH list as well

3. If anything gives you trouble, run a quick `make clean` to clear out any old object files

---

## How to Update the ASF Library:

1. Go to [Atmel Start](https://start.atmel.com/) and use the `atmel_start_config.atstart` file in the root of this repository to import the project.
2. Make any desired changes.
3. Export the project with the Makefile box checked, and the name ASF.
   - This will result in a file named `ASF.atzip`. Do not change this name.
4. Place the `ASF.atzip` file in the root of this repository.
5. Run `make update_asf` to update the ASF library. This will completely wipe the ASF folder, so be careful!
   - Because of this, you should not put anything in the ASF folder that is not autogenerated by Atmel Start.
6. Ideally, there should be nothing to be done after `make update_asf` completes. Try to `make clean` and `make` to verify it all works.
