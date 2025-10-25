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

# NOTE(blackedout): Path is only set by buildlibs.sh if quotes are present
vulkan_sdk=""

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

# NOTE(blackedout): Compile the program
include_paths="-I$vulkan_sdk_platform/include -Iglfw/include"
library_paths="-L$vulkan_sdk_platform/lib"
defines="-DGLFW_INCLUDE_VULKAN"
explicit_layer_define="-DVULKAN_EXPLICIT_LAYERS_PATH=\"$vulkan_sdk_platform/share/vulkan/explicit_layer.d\""
shared_a=bin/glfw.a

if [ -e "bin/vma.a" ]; then
    include_paths="$include_paths -IVulkanMemoryAllocator/include"
    defines="$defines -DVULKAN_USE_VMA"
    shared_a="$shared_a bin/vma.a"
    libraries="$libraries $vma_libcpp"
fi

shaders_dst=bin/shaders
if [ "$is_macos" = true ]; then

    # NOTE(blackedout): Create independent bundled application if the first argument to calling this script is bundle.
    # The independence from the Vulkan SDK is created by copying relevant dynamic library and JSON files into the bundle and correctly linking it up.
    if [ "$1" = "bundle" ]; then
        rpath=@loader_path/../Frameworks

        # NOTE(blackedout): Apparently there is nothing special about a macOS bundled application program. It is just a bunch of directories, which macOS then displays with a different icon
        # https://stackoverflow.com/questions/1596945/building-osx-app-bundle (2025-10-25)
        bundle_name="template"
        bundle_contents="$bundle_name.app/Contents"

        # NOTE(blackedout): Create bundle directoy structure like documented in (the vulkan folder is documented in the next source)
        # https://developer.apple.com/library/archive/documentation/CoreFoundation/Conceptual/CFBundles/BundleTypes/BundleTypes.html#//apple_ref/doc/uid/10000123i-CH101-SW1 (2025-10-25)
        mkdir -p "$bundle_contents/MacOS" "$bundle_contents/Frameworks" "$bundle_contents/Resources/vulkan"

        # NOTE(blackedout): Copy Vulkan layer and driver JSON files from SDK to vulkan folder in bundle Resources like documented in
        # https://www.lunarg.com/wp-content/uploads/2022/05/The-State-of-Vulkan-on-Apple-15APR2022.pdf (2025-10-25)
        cp -r "$vulkan_sdk/macOS/share/vulkan/explicit_layer.d" $bundle_contents/Resources/vulkan
        cp -r "$vulkan_sdk/macOS/share/vulkan/icd.d" $bundle_contents/Resources/vulkan

        # NOTE(blackedout): Copy Vulkan loader from SDK to bundle Frameworks and create relative link files (version needs to be broken down for this to work correctly)
        vulkan_sdk_version=`cat vulkan/latest_sdk_version.txt`
        versions=(${vulkan_sdk_version//./ })
        cp "$vulkan_sdk/macOS/lib/libvulkan.${versions[0]}.${versions[1]}.${versions[2]}.dylib" "$bundle_contents/Frameworks"
        ln -sf "libvulkan.${versions[0]}.${versions[1]}.${versions[2]}.dylib" "$bundle_contents/Frameworks/libvulkan.${versions[0]}.dylib"
        ln -sf "libvulkan.${versions[0]}.${versions[1]}.${versions[2]}.dylib" "$bundle_contents/Frameworks/libvulkan.dylib"

        # NOTE(blackedout): Copy MolenVK and all Vulkan layers from SDK to bundle Frameworks
        cp "$vulkan_sdk/macOS/lib/libMoltenVK.dylib" "$bundle_contents/Frameworks"
        for f in $vulkan_sdk/macOS/lib/libVkLayer*.dylib; do
            cp "$f" "$bundle_contents/Frameworks"
        done

        # NOTE(blackedout): Remove relative paths (sed -i backup_extension 's@regex@replacement@g' file)
        # The JSON files point to the correct library files in the Vulkan SDK itself by using "../../../lib/" as a prefix to the library files.
        # In the bundle these are not correct anymore. Just the library files suffice, however, so just remove the prefixes
        sed -i '' 's@\.\.\/\.\.\/\.\.\/lib\/lib@lib@g' $bundle_contents/Resources/vulkan/*/*.json

        # NOTE(blackedout): Change compiler outputs to point into the bundle and have the bundle's name in case of the executable itself
        compiler_output=$bundle_contents/MacOS/$bundle_name
        shaders_dst=$bundle_contents/Resources/$shaders_dst
    else
        rpath=$vulkan_sdk_platform/lib
        moltenvk_driver="$vulkan_sdk_platform/share/vulkan/icd.d/MoltenVK_icd.json"
        defines="$defines $explicit_layer_define -DVULKAN_DRIVER_FILES=\"$moltenvk_driver\""

        compiler_output=a.out
    fi
    
    clang -g -O0 -Wall $include_paths $library_paths $defines main.c $shared_a $libraries -Wl,-rpath,$rpath -o$compiler_output
else
    # -Wno-missing-braces is to avoid console spamming (for a compiler bug?)
    defines="$defines $explicit_layer_define"
    gcc -g -O0 -Wall -Wno-missing-braces $include_paths $library_paths $defines main.c $shared_a -lm $libraries -Wl,--disable-new-dtags,-rpath=$vulkan_sdk_platform/lib
fi

# NOTE(blackedout): Compile shaders
mkdir -p $shaders_dst

glslc="$vulkan_sdk_platform/bin/glslc"
$glslc shaders/default.vert -o $shaders_dst/default.vert.spv
$glslc shaders/default.frag -o $shaders_dst/default.frag.spv