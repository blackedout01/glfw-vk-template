vulkan_sdk="/Applications/VulkanSDK"

mkdir -p bin

# Retrieve if platform is macOS
is_macos=false
if [[ "$OSTYPE" =~ ^darwin* ]]; then
    is_macos=true
fi

# Set Vulkan SDK path in build.sh (double quotes allow for variables while single quotes don't, the escaping is done for sh)
sed -i '' "s@vulkan_sdk=\"[^\"]*\"@vulkan_sdk=\"$vulkan_sdk\"@g" build.sh

# Vulkan (macOS only) - https://gist.github.com/Resparing/d30634fcd533ec5b3235791b21265850 (2024-07-03)
if [ "$is_macos" = true ]; then
    # Copy .json files
    dirname="vulkan/explicit_layer.d" && mkdir -p $dirname && cp $vulkan_sdk/macOS/share/$dirname/*.json $dirname/
    dirname="vulkan/icd.d" && mkdir -p $dirname && cp $vulkan_sdk/macOS/share/$dirname/*.json $dirname/

    # Remove relative paths (sed -i backup_extension 's@regex@replacement@g' file)
    sed -i '' 's@\.\.\/\.\.\/\.\.\/lib\/lib@lib@g' vulkan/*/*.json
fi

# GLFW
glfw_src="glfw/src"

glfw_src_shared="context.c init.c input.c monitor.c platform.c vulkan.c window.c egl_context.c osmesa_context.c null_init.c null_monitor.c null_window.c null_joystick.c"

glfw_src_apple="cocoa_time.c posix_module.c posix_thread.c"
glfw_src_other="posix_time.c posix_module.c posix_thread.c"

glfw_src_cocoa="cocoa_init.m cocoa_joystick.m cocoa_monitor.m cocoa_window.m nsgl_context.m"

glfw_src_x11="x11_init.c x11_monitor.c x11_window.c xkb_unicode.c glx_context.c posix_poll.c linux_joystick.c"
glfw_src_wl="wl_init.c wl_monitor.c wl_window.c xkb_unicode.c posix_poll.c linux_joystick.c"

cd $glfw_src
echo "Compiling GLFW"
if [ "$is_macos" = true ]; then
    clang -c -g -O0 -D_GLFW_COCOA $glfw_src_shared $glfw_src_apple $glfw_src_cocoa
else
    echo ""
fi
cd ~-

ar rc bin/glfw.a $glfw_src/*.o
rm $glfw_src/*.o