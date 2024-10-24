:: Original source in https://github.com/blackedout01/glfw-vk-template

:: This is free and unencumbered software released into the public domain.
:: Anyone is free to copy, modify, publish, use, compile, sell, or distribute
:: this software, either in source code form or as a compiled binary, for any
:: purpose, commercial or non-commercial, and by any means.
::
:: In jurisdictions that recognize copyright laws, the author or authors of
:: this software dedicate any and all copyright interest in the software to the
:: public domain. We make this dedication for the benefit of the public at
:: large and to the detriment of our heirs and successors. We intend this
:: dedication to be an overt act of relinquishment in perpetuity of all present
:: and future rights to this software under copyright law.
::
:: THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
:: IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
:: FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
:: AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
:: ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
:: WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
::
:: For more information, please refer to https://unlicense.org

@echo off

set vulkan_sdk=C:\VulkanSDK

set options=/DEBUG /Z7

set include_paths=/I%vulkan_sdk%\Include /Iglfw\include
set library_paths=/LIBPATH:%vulkan_sdk%\Lib

if not exist %vulkan_sdk%\Include\ (
    echo Vulkan SDK path set incorrectly: '%vulkan_sdk%\Include' does not exist.
    echo Please specify the correct SDK path.
    exit /b
)

set libs=bin\glfw.lib
set defines=/DGLFW_INCLUDE_VULKAN
if exist bin\vma.lib (
    set libs=%libs% bin\vma.lib
    set defines=%defines% /DVULKAN_USE_VMA
    set include_paths=%include_paths% /IVulkanMemoryAllocator\include
)

:: Use /std:c++20 if compiling in C++ mode
cl /nologo %options% %include_paths% %defines% main.c %libs% /link %library_paths% vulkan-1.lib User32.lib Gdi32.lib Shell32.lib

if not exist bin\shaders md bin\shaders

set glslc=%vulkan_sdk%\bin\glslc.exe
%glslc% shaders/default.vert -o bin/shaders/default.vert.spv
%glslc% shaders/default.frag -o bin/shaders/default.frag.spv
