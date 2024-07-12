@echo off

set vulkan_sdk=C:\Users\Kilian\VulkanSDK

set include_paths=/I%vulkan_sdk%\Include /Iglfw\include
set library_paths=/LIBPATH:%vulkan_sdk%\Lib

cl /nologo /DEBUG /Z7 %include_paths% main.c bin\glfw.lib /link %library_paths% vulkan-1.lib User32.lib Gdi32.lib Shell32.lib


if not exist bin\shaders md bin\shaders

set glslc=%vulkan_sdk%\bin\glslc.exe
%glslc% shaders/default.vert -o bin/shaders/default.vert.spv
%glslc% shaders/default.frag -o bin/shaders/default.frag.spv
