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


## Build on Linux (GCC)
Currently, only x86_64 architecture is supported.

#### 1. Install the Vulkan SDK and configure `buildlibs.sh`
Download the SDK as a `.tar.xz` file from https://vulkan.lunarg.com/sdk/home#linux and extract it.
<br/>In the `buildlibs.sh` file, modify the line that sets the variable `vulkan_sdk` to contain the path to your Vulkan SDK. Important: the correct directory is the one that contains `LICENSE.txt` and the path should be absolute.

#### 2. Install X11 development dependencies
This is neccessary to compile the GLFW library for X11. See [Dependencies for X11 on Unix-like systems](https://www.glfw.org/docs/3.3/compile.html#compile_deps_x11) for details.
On Debian and derivatives like Ubuntu and Linux Mint you need to run:

```
sudo apt install xorg-dev
```
#### 3. Compile the libraries
Running the buildlibs script compiles all library source files into one file for each library, e.g. `bin/glfw.a` for GLFW. It also copies the `vulkan_sdk` variable to the `build.sh` file. You only need to do this once.
```
sh buildlibs.sh
```

#### 4. Compile the program
The build script compiles the program and links neccesary libaries. The output file is `a.out`. It also compiles the two default shader files into `bin/shaders`.
```
sh build.sh
```

#### 5. Run the program
```
./a.out
```
