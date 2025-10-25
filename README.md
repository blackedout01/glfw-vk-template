## glfw-vk-template
A GLFW Vulkan template project that is intended to help you in quickly setting up new vulkan projects. No complicated build steps neccessary. Just you and two shell scripts.
<br/>There are also some VS Code files included for IntelliSense and debugging. However, because it doesn't exactly understand source file inclusion, it seems you have to open `main.c` first and let it parse before allowing the other files to be processed.
<br/>On macOS, this project is also an example of how a working bundled application that uses Vulkan can be created (independent of the Vulkan SDK). Though this is only concerning the "working" part. It may still be invalid according to some Apple specs, since it is for example missing an `Info.plist`.

Note: The usage of the Vulkan Memory Allocator library is optional. It makes memory allocations a lot easier but if you don't need it, just remove it as a submodule (and the directory). The build scripts only compile it if there is a directory `VulkanMemoryAllocator` (buildlibs) and only add it if there is a file `bin/vma.a` or `bin/vma.lib` (build).

GLFW is checked out to release 3.4.
<br/>VulkanMemoryAllocator is checked out to release v3.1.0.

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

#### OPTIONAL: 1. Install the Vulkan SDK and configure `buildlibs.sh`
**This step is now optional, because the latest Vulkan SDK will be downloaded and extracted (not installed) locally and automatically if no path is specified as is in the `buildlibs.sh` script. See step 3 for details. However, you may still want to install the SDK globally if you have multiple Vulkan projects (the SDK is 2 to 3 GB).**
<br/>Download the SDK from https://vulkan.lunarg.com/sdk/home#mac and install it.
<br/>In the `buildlibs.sh` file, modify the line that sets the variable `vulkan_sdk` to contain the path to your Vulkan SDK.
<br/>⚠️ The path should be absolute and reference the directory that contains a `macOS` subdirectory. For example, if the path of this subdirectory is `/Applications/VulkanSDK/macOS`, the line needs to be changed to:
```
vulkan_sdk="/Applications/VulkanSDK"
```
If you want to use the project in VS Code you should also modify the `includePath` in the Mac section of `.vscode/c_cpp_properties.json` to use the correct Vulkan SDK path:
```
"includePath": [
    "${workspaceFolder}/**",
    "/Applications/VulkanSDK/macOS/include"
],
```

#### 2. Install your build tools
You can do this by installing Xcode.

#### 3. Compile the libraries
Running the buildlibs script compiles all library source files into one file for each library, e.g. `bin/glfw.a` for GLFW.
<br/>Additionally, if the `vulkan_sdk` variable is left empty like `vulkan_sdk=`, then this script also downloads the latest Vulkan SDK from `vulkan.lunarg.com` and extracts it into a `vulkan` folder.
Alternatively if you want to avoid this you can set the `vulkan_sdk` variable to point to a valid macOS Vulkan SDK that you have installed.
In both cases the path to the Vulkan SDK (either specified by you or the local one) is copied to the `vulkan_sdk` variable of the `build.sh` file.
<br/>You only need to call this script once (like a project setup).
```
sh buildlibs.sh
```

**IMPORTANT: Automatically extracting the Vulkan SDK from the installer uses `--accept-licenses`, which assumes that you agree with the licenses of the SDK which would otherwise be displayed in the installer's GUI. You may want to read them before proceeding to use the SDK.**

#### 4. Compile the program
The build script compiles the program and links neccesary libaries. The output file is `a.out`, which, for convenience of execution, is not stored in the `bin` directory. The script also compiles the two default shader files into `bin/shaders`.
```
sh build.sh
```

In order to create a program that is independent of the installed Vulkan SDK, the build script may instead be called with the "bundle" argument, to create a macOS bundled application. This is not indendet for regular development, as it copies a few large files.
```
sh build.sh bundle
```

By default the bundle name is `template.app`. See the code of the script for more details. Note however, that this bundle is incomplete, because it is missing the crucial `Info.plist` file, which doesn't seem to be important to just make the bundle runnable.

#### 5. Run the program
```
./a.out
```
Step 4 and 5 can also be done by pressing F5 in VS Code, if the launch configuration "macOS" is selected.

## Build on Linux (GCC)
Currently, only x86_64 architecture is supported.

#### 1. Install the Vulkan SDK and configure `buildlibs.sh`
Download the SDK as a `.tar.xz` file from https://vulkan.lunarg.com/sdk/home#linux and extract it.
<br/>In the `buildlibs.sh` file, modify the line that sets the variable `vulkan_sdk` to contain the path to your Vulkan SDK.
<br/>⚠️ The path should be absolute and reference the directory that contains an `x86_64` subdirectory. For example, if the path of this subdirectory is `/home/me/VulkanSDK/x86_64`, the line needs to be changed to:
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
The build script compiles the program and links neccesary libaries. The output file is `a.out`, which, for convenience of execution, is not stored in the `bin` directory. The script also compiles the two default shader files into `bin/shaders`.
```
sh build.sh
```

#### 5. Run the program
```
./a.out
```
Step 4 and 5 can also be done by pressing F5 in VS Code, if the launch configuration "Linux" is selected.

## Build on Windows (MSVC)

#### 1. Install the Vulkan SDK and configure `buildlibs.bat` and `build.bat`
Download the SDK installer from https://vulkan.lunarg.com/sdk/home#windows and install.
<br/>In the `buildlibs.bat` **and** `build.bat` file, modify the line that sets the variable `vulkan_sdk` to contain the path to your Vulkan SDK.
<br/>⚠️ The path should be absolute and reference the directory that contains an `Include` subdirectory. For example, if the path of this subdirectory is `C:\VulkanSDK\Include`, the line needs to be changed to:
```
set vulkan_sdk=C:\VulkanSDK
```
If you want to use the project in VS Code you should also modify the `includePath` in `.vscode/c_cpp_properties.json` to use the correct Vulkan SDK path.

#### 2. Install your build tools
You can do this by installing the [Build Tools for Visual Studio 2022](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022). This is not necessary if you have already installed Visual Studio (the IDE, not VS Code).
<br/>In your list of programs you should now have a directory "Visual Studio 2022" with a special console "x64 Native Tools Command Prompt for VS 2022" which you can use to compile C or C++ using Microsofts cl.exe compiler. I recommend to pin this into your start menu. The following commands have to be executed in this console.
#### 3. Compile the libraries
Running the buildlibs script compiles all library source files into one file for each library, e.g. `bin/glfw.lib` for GLFW. You only need to do this once.
```
buildlibs.bat
```

#### 4. Compile the program
The build script compiles the program and links neccesary libaries. The output file is `main.exe`, which, for convenience of execution, is not stored in the `bin` directory. The script also compiles the two default shader files into `bin/shaders`.
```
build.bat
```

#### 5. Run the program
```
main
```
If you get an error that a Vulkan dll could not be found, make sure to also install the Vulkan runtime from here https://vulkan.lunarg.com/sdk/home#windows using the installer.
<br/>Step 4 and 5 can also be done by pressing F5 in VS Code, if the launch configuration "Windows" is selected.
