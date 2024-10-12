# Original source in https://github.com/blackedout01/glfw-vk-template
#
# This is free and unencumbered software released into the public domain.
# Anyone is free to copy, modify, publish, use, compile, sell, or distribute
# this software, either in source code form or as a compiled binary, for any
# purpose, commercial or non-commercial, and by any means.
#
# In jurisdictions that recognize copyright laws, the author or authors of
# this software dedicate any and all copyright interest in the software to the
# public domain. We make this dedication for the benefit of the public at
# large and to the detriment of our heirs and successors. We intend this
# dedication to be an overt act of relinquishment in perpetuity of all present
# and future rights to this software under copyright law.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# For more information, please refer to https://unlicense.org

vulkan_sdk="/home/kilian/VulkanSDK"

if [ "$(uname)" = "Darwin" ]; then
    is_macos=true
    vulkan_sdk_platform="$vulkan_sdk/macOS"
    vma_libcpp=-lc++
    libraries="-framework Cocoa -framework IOKit -lvulkan"
else
    is_macos=false
    vulkan_sdk_platform="$vulkan_sdk/x86_64"
    vma_libcpp=-lstdc++
    libraries="-lm -lvulkan"
fi

include_paths="-I$vulkan_sdk_platform/include -Iglfw/include"
library_paths="-L$vulkan_sdk_platform/lib"
defines="-DGLFW_INCLUDE_VULKAN -DVULKAN_EXPLICIT_LAYERS_PATH=\"$vulkan_sdk_platform/share/vulkan/explicit_layer.d\""
shared_a=bin/glfw.a

if [ -e "bin/vma.a" ]; then
    include_paths="$include_paths -IVulkanMemoryAllocator/include"
    defines="$defines -DVULKAN_USE_VMA"
    shared_a="$shared_a bin/vma.a"
    libraries="$libraries $vma_libcpp"
fi

if [ "$is_macos" = true ]; then
    moltenvk_driver="$vulkan_sdk_platform/share/vulkan/icd.d/MoltenVK_icd.json"
    defines="$defines -DVULKAN_DRIVER_FILES=\"$moltenvk_driver\""
    clang -g -O0 $include_paths $library_paths $defines main.c $shared_a $libraries -Wl,-rpath,$vulkan_sdk_platform/lib
else
    gcc -g -O0 $include_paths $library_paths $defines main.c $shared_a -lm $libraries -Wl,--disable-new-dtags,-rpath=$vulkan_sdk_platform/lib
fi

# Compile all shaders
mkdir -p bin/shaders

glslc="$vulkan_sdk_platform/bin/glslc"
$glslc shaders/default.vert -o bin/shaders/default.vert.spv
$glslc shaders/default.frag -o bin/shaders/default.frag.spv
