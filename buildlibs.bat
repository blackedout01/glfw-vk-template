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

set bin_path=bin

if not exist %bin_path% md %bin_path%

set glfw_src=glfw\src
set glfw_src_shared=context.c init.c input.c monitor.c platform.c vulkan.c window.c egl_context.c osmesa_context.c null_init.c null_monitor.c null_window.c null_joystick.c
set glfw_src_windows=win32_time.c win32_module.c win32_thread.c
set glfw_src_win32=win32_init.c win32_joystick.c win32_monitor.c win32_window.c wgl_context.c

pushd .
cd %glfw_src%
echo Compiling GLFW
cl -c -nologo -DEBUG -Z7 -D_GLFW_WIN32 %glfw_src_shared% %glfw_src_windows% %glfw_src_win32%
popd

lib -nologo %glfw_src%\*.obj -out:bin/glfw.lib
del %glfw_src%\*.obj