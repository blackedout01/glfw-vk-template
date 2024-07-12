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