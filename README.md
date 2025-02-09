**dlssg-to-fsr3** is a drop-in mod/replacement for games utilizing [Nvidia's DLSS-G Frame Generation](https://nvidianews.nvidia.com/news/nvidia-introduces-dlss-3-with-breakthrough-ai-powered-frame-generation-for-up-to-4x-performance) technology that allows people to use [AMD's FSR 3 Frame Generation](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) technology instead. Only RTX 1600, RTX 2000, and RTX 3000 series GPUs are currently supported.

Using dlssg-to-fsr3 in multiplayer games is ill advised and may lead to account bans. **Use at your own risk.**

## Download Link

[https://www.nexusmods.com/site/mods/738](https://www.nexusmods.com/site/mods/738?tab=files)

## Installation (User)

1. Double click on `DisableNvidiaSignatureChecks.reg` and select **Run**. Click **Yes** on the next few dialogs.
2. Find your game's installation folder. For Cyberpunk 2077, this is the directory containing `Cyberpunk2077.exe`. An example path is `C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64\`.
3. Copy `dlssg_to_fsr3_amd_is_better.dll` and the new `nvngx.dll` to your game's installation folder.
4. A log file named `dlssg_to_fsr3.log` will be created after you launch the game.

## Installation (Developer)

1. Open `CMakeUserEnvVars.json` with a text editor and rename `___GAME_ROOT_DIRECTORY` to `GAME_ROOT_DIRECTORY`.
2. Change the path in `GAME_ROOT_DIRECTORY` to your game of choice. Built DLLs are automatically copied over.
3. Change the path in `GAME_DEBUGGER_CMDLINE` to your executable of choice. This allows direct debugging from Visual Studio's interface.
4. Manually copy `resources\dlssg_to_fsr3.ini` to the game directory for FSR 3 visualization and debug options.

## Building

### Requirements

- This repository and all of its submodules cloned.
- The [Vulkan SDK](https://vulkan.lunarg.com/) and `VULKAN_SDK` environment variable set.
- **Visual Studio 2022** 17.9.6 or newer.
- **CMake** 3.26 or newer.
- **Vcpkg**.

### FidelityFX SDK

1. Open a `Visual Studio 2022 x64 Tools Command Prompt` instance.
2. Navigate to the `dependencies\FidelityFX-SDK\sdk\` subdirectory.
3. Run `BuildFidelityFXSDK.bat` and wait for compilation.
4. Done.

### dlssg-to-fsr3 (Option 1, Visual Studio UI)

1. Open `CMakeLists.txt` directly or open the root folder containing `CMakeLists.txt`.
2. Select one of the preset configurations from the dropdown, e.g. `Universal Release x64`.
3. Build and wait for compilation.
4. Build files are written to the bin folder. Done.

### dlssg-to-fsr3 (Option 2, Powershell Script)

1. Open a Powershell command window.
2. Run `.\Make-Release.ps1` and wait for compilation.
3. Build files from each configuration are written to the bin folder and archived. Done.

## License

- [GPLv3](LICENSE.md)
- [Third party licenses](/resources/binary_dist_license.txt)