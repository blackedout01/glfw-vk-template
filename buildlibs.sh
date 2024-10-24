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

vulkan_sdk="/Applications/VulkanSDK"

mkdir -p bin

# Argument for sed: set Vulkan SDK path in build.sh (double quotes allow for variables while single quotes don't, the escaping is done for sh)
sed_arg="s@vulkan_sdk=\"[^\"]*\"@vulkan_sdk=\"$vulkan_sdk\"@g"

if [ "$(uname)" = "Darwin" ]; then
    is_macos=true
    sed_i=$'\0'
    vulkan_sdk_platform="$vulkan_sdk/macOS"
    sed -i '' $sed_arg build.sh
else
    is_macos=false
    sed_i=
    vulkan_sdk_platform="$vulkan_sdk/x86_64"
    sed -i $sed_arg build.sh
fi

if [ ! -d "$vulkan_sdk_platform" ]; then
    echo "Vulkan SDK path set incorrectly: '$vulkan_sdk_platform' does not exist."
    echo "Please specify the correct SDK path."
    exit 1
fi

# GLFW
glfw_src="glfw/src"
glfw_dst="bin/glfw.a"

if [ ! -d "$glfw_src" ]; then
    echo "GLFW not cloned."
    echo "Run 'git submodule update --init' to fix."
    exit 1
fi

glfw_files_shared="context.c init.c input.c monitor.c platform.c vulkan.c window.c egl_context.c osmesa_context.c null_init.c null_monitor.c null_window.c null_joystick.c"

glfw_files_apple="cocoa_time.c posix_module.c posix_thread.c"
glfw_files_other="posix_time.c posix_module.c posix_thread.c"

glfw_files_cocoa="cocoa_init.m cocoa_joystick.m cocoa_monitor.m cocoa_window.m nsgl_context.m"
glfw_files_x11="x11_init.c x11_monitor.c x11_window.c xkb_unicode.c glx_context.c posix_poll.c linux_joystick.c"
glfw_files_wl="wl_init.c wl_monitor.c wl_window.c xkb_unicode.c posix_poll.c linux_joystick.c"

cd "$glfw_src"
echo "Compiling GLFW into $glfw_dst"
if [ "$is_macos" = true ]; then
    clang -c -g -O0 -D_GLFW_COCOA $glfw_files_shared $glfw_files_apple $glfw_files_cocoa
else
    gcc -c -g -O0 -D_GLFW_X11 $glfw_files_shared $glfw_files_other $glfw_files_x11
fi
cd "$OLDPWD"

ar rc $glfw_dst $glfw_src/*.o
rm $glfw_src/*.o

# Vulkan Memory Allocator
vma_src="VulkanMemoryAllocator/include"
vma_dst="bin/vma.a"

if [ -d "$vma_src" ]; then
    echo "Compiling VulkanMemoryAllocator into $vma_dst"
    cd $vma_src
    if [ "$is_macos" = true ]; then
        clang++ -std=c++11 -c -g -O0 -DVMA_IMPLEMENTATION -xc++ "vk_mem_alloc.h" -I"$vulkan_sdk_platform/include"
    else
        g++ -std=c++17 -c -g -O0 -DVMA_IMPLEMENTATION -xc++ "vk_mem_alloc.h" -I"$vulkan_sdk_platform/include"
    fi
    cd "$OLDPWD"

    ar rc $vma_dst $vma_src/*.o
    rm $vma_src/*.o
else
    echo "'$vma_src' does not exist, skipping VulkanMemoryAllocator"
    if [ -e "$vma_dst" ]; then
        rm $vma_dst
    fi
fi