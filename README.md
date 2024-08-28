## License
I haven't decided on a license for the C and shader code yet, but the build scripts are now public domain.

## Download
`git clone --recursive https://github.com/blackedout01/glfw-vk-template.git`
<br/>or if already cloned without `--recursive`:
<br/>`git submodule update --init`

## Build on macOS

### Step 0
Download the latest Vulkan SDK installer for macOS here https://vulkan.lunarg.com/sdk/home#mac and install.
<br/>Then modify the very first line of `buildlibs.sh` in this repository such that the variable `vulkan_sdk` contains the path to the root of the Vulkan SDK. For example, if you've installed the SDK into `/Applications/VulkanSDK`, then set the first line to:
```
vulkan_sdk="/Applications/VulkanSDK"
```

### Clang
**Install your build tools (e.g. Xcode)**
<br/>**Open terminal and navigate to repository**
<br/>**Build all libraries once:** `sh buildlibs.sh`
<br/>Now there is a new file `bin/glfw.a`, which contains the compiled source code of GLFW usable with macOS. Additionally, the `vulkan_sdk` variable is copied into `build.sh`.
<br/>**Build project:** `sh build.sh`
<br/>This compiles the program and the default shader located in `shaders` (into `bin/shaders`). For convenience of execution, the program is not stored in the `bin` folder.
<br/>**Run project:** `./a.out`

## Build on Windows

### Step 0
Download the latest Vulkan SDK installer for Windows here https://vulkan.lunarg.com/sdk/home#windows and install.
<br/>Then modify the third line of `build.sh` in this repository such that the variable `vulkan_sdk` contains the path to the root of the Vulkan SDK. For example, if you've installed the SDK into `C:\User\Me\VulkanSDK`, then set the third line to:
```
set vulkan_sdk=C:\Users\Me\VulkanSDK
```

### MSVC
**Install Visual Studio Build Tools** https://visualstudio.microsoft.com/downloads/ (under "All Downloads" then "Tools for Visual Studio" and finally at "Build Tools for Visual Studio 2022")
<br/>**Open "x64 Native Tools Command Prompt for VS 2022" and navigate to repository**
<br/>**Build all libraries once:** `buildlibs.bat`
<br/>Now there is a new file `bin\glfw.lib`, which contains the compiled source code of GLFW usable with Windows.
<br/>**Build project:** `build.bat`
<br/>This compiles the program and the default shader located in `shaders` (into `bin\shaders`). For convenience of execution, the program is not stored in the `bin` folder.
<br/>**Run project:** `main`

## Build on Linux
TBD