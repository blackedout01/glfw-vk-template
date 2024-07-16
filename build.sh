vulkan_sdk="/Applications/VulkanSDK"

include_paths="-I$vulkan_sdk/macOS/include -Iglfw/include"
library_paths="-L$vulkan_sdk/macOS/lib"

clang -g -O0 $include_paths $library_paths -DGLFW_INCLUDE_VULKAN main.c bin/glfw.a -framework Cocoa -framework IOKit -Wl,-rpath,$vulkan_sdk/macOS/lib -lvulkan

# Compile all shaders
mkdir -p bin/shaders

glslc="$vulkan_sdk/macOS/bin/glslc"

$glslc shaders/default.vert -o bin/shaders/default.vert.spv
$glslc shaders/default.frag -o bin/shaders/default.frag.spv
