@echo off
setlocal

if "%VK_SDK_PATH%"=="" (
    echo VK_SDK_PATH is not set.
    exit /b 1
)

if "%~1"=="" (
    echo Missing output directory argument.
    exit /b 1
)

set "OUTPUT_DIR=%~1"
set "SHADER_DIR=%~dp0..\Shaders\test"
set "DXC=%VK_SDK_PATH%\Bin\dxc.exe"

if not exist "%DXC%" (
    echo Could not find dxc at "%DXC%".
    exit /b 1
)

if not exist "%OUTPUT_DIR%" (
    mkdir "%OUTPUT_DIR%"
)

"%DXC%" -spirv -fspv-target-env=vulkan1.3 -T vs_6_0 -E main -Fo "%OUTPUT_DIR%\triangle_vs.spv" "%SHADER_DIR%\triangle.vs.hlsl"
if errorlevel 1 exit /b 1

"%DXC%" -spirv -fspv-target-env=vulkan1.3 -T ps_6_0 -E main -Fo "%OUTPUT_DIR%\triangle_ps.spv" "%SHADER_DIR%\triangle.ps.hlsl"
if errorlevel 1 exit /b 1
