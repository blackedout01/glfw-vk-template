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
<br/>This compiles the program and the default shader localted in `shaders` (into `bin/shaders`). For convenience of execution, the program is not stored in the `bin` folder.
<br/>**Run project:** `./a.out`

## Build on Windows
TBD

## Build on Linux
TBD