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

failmsg () {
    echo
    echo $1
	exit 1
}

# NOTE(blackedout): Use this to make script callable from other locations
script_dir=$(dirname $0)

if [ ! "$(uname)" = "Darwin" ]; then
    failmsg "Currently only supported on macOS."
fi

# NOTE(blackedout): Documented at https://vulkan.lunarg.com/content/view/latest-sdk-version-api (2025-10-15)
echo "Fetching latest Vulkan SDK version from lunarg.com..."
latest_sdk_version=$(curl --no-progress-meter https://vulkan.lunarg.com/sdk/latest/mac.txt)
if [ $? == 0 ]; then
    vulkan_dst="$script_dir/vulkan"
    mkdir -p $vulkan_dst

    sdk_name="vulkansdk-macos-$latest_sdk_version"
    sdk_archive_name="$sdk_name.zip"
    sdk_installer_name="$sdk_name.app"
    sdk_url="https://sdk.lunarg.com/sdk/download/$latest_sdk_version/mac/$sdk_archive_name"

    # NOTE(blackedout): Download SDK installer if it doesn't exist already
    sdk_archive_dst="$vulkan_dst/$sdk_archive_name"
    if [ ! -e $sdk_archive_dst ]; then
        echo "Downloading the Vulkan SDK installer at '$sdk_url'..."
        curl -o $sdk_archive_dst -f -L $sdk_url
        if [ $? != 0 ]; then
            failmsg "Failed."
        fi
    else
        echo "Archive '$sdk_archive_dst' exists. Delete to redownload."
    fi

    # NOTE(blackedout): Extract the SDK archive if it isn't extracted already
    sdk_installer_dst="$vulkan_dst/$sdk_installer_name"
    if [ ! -d $sdk_installer_dst ]; then
        echo "Extracting '$sdk_archive_dst'..."
        unzip $sdk_archive_dst -d "$vulkan_dst"
        if [ $? != 0 ]; then
            failmsg "Failed."
        fi
    else
        echo "Installer '$sdk_installer_dst' exists. Delete to reextract."
    fi

    sdk_dst="$vulkan_dst/$sdk_name"
    if [ ! -d $sdk_dst ]; then
        # NOTE(blackedout): Documented at https://vulkan.lunarg.com/doc/sdk/latest/mac/getting_started.html (Install and Uninstall from Terminal, 2025-10-15)
        echo "Running installer in unattended mode using copy_only=1 to prevent system changes..."
        ./$sdk_installer_dst/Contents/MacOS/$sdk_name --root "$PWD/$vulkan_dst/$sdk_name" --accept-licenses --default-answer --confirm-command install copy_only=1
        if [ $? != 0 ]; then
            failmsg "Failed."
        fi
    fi

    # NOTE(blackedout): Write out only on success
    latest_sdk_version_dst="$vulkan_dst/latest_sdk_version.txt"
    echo "Writing latest sdk version '$latest_sdk_version' to '$latest_sdk_version_dst'..."
    echo $latest_sdk_version > $latest_sdk_version_dst

    echo "SDK is available at '$sdk_dst'."
else
    echo "Failed. Skipping."
fi


