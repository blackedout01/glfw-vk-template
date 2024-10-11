## glfw-vk-template
A GLFW Vulkan template project that is intended to help you in quickly setting up new vulkan projects. No complicated build steps neccessary. Just you and two shell scripts.

## License
Build scripts, shader code and `main.c` are public domain. The other files are zlib licensed (because you should really be writing them yourself and also there are probably errors in mine). You can use the VS Code files as you wish too (not even sure what most of the stuff in there means).

## Download
```
git clone --recursive https://github.com/blackedout01/glfw-vk-template.git
```
If you have already cloned without `--recursive`, run the followinig command inside of the repository:
```
git submodule update --init
```

## Build on macOS (clang)

#### 1. Install the Vulkan SDK and configure `buildlibs.sh`
Download the SDK from https://vulkan.lunarg.com/sdk/home#mac and install it.
<br/>In the `buildlibs.sh` file, modify the line that sets the variable `vulkan_sdk` to contain the path to your Vulkan SDK.
<br/>⚠️ The path should be absolute and reference the directory that contains a `macOS` subdirectory. For example, if the path of this subdirectory is `/Applications/VulkanSDK/macOS`, the line needs to be changed to:
```
vulkan_sdk="/Applications/VulkanSDK"
```
If you want to use the project in VS Code you should also modify the `includePath` in `.vscode/c_cpp_properties.json` to use the correct Vulkan SDK path.

#### 2. Install your build tools
You can do this by installing Xcode.

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

## Build on Linux (GCC)
Currently, only x86_64 architecture is supported.

#### 1. Install the Vulkan SDK and configure `buildlibs.sh`
Download the SDK as a `.tar.xz` file from https://vulkan.lunarg.com/sdk/home#linux and extract it.
<br/>In the `buildlibs.sh` file, modify the line that sets the variable `vulkan_sdk` to contain the path to your Vulkan SDK.
<br/>⚠️ The path should be absolute and reference the directory that contain an `x86_64` subdirectory. For example, if the path of this subdirectory is `/home/me/VulkanSDK/x86_64`, the line needs to be changed to:
```
vulkan_sdk="/home/me/VulkanSDK"
```
If you want to use the project in VS Code you should also modify the `includePath` in `.vscode/c_cpp_properties.json` to use the correct Vulkan SDK path.

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
