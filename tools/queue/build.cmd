@echo off
rem Configures, builds and tests the plugin in build\ with the x86 MSVC toolchain.
rem Meant to run as a shared-queue job, which does not inherit a developer shell.
rem Usage: build.cmd [fresh] [package]
rem   fresh    discards the CMake cache first
rem   package  after the tests pass, writes both release ZIPs to dist\
setlocal
set "REPO=%~dp0..\.."
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1

set "FRESH="
set "PACKAGE="
for %%A in (%*) do (
    if /i "%%~A"=="fresh" set "FRESH=--fresh"
    if /i "%%~A"=="package" set "PACKAGE=1"
)

cmake %FRESH% -S "%REPO%" -B "%REPO%\build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || exit /b 1
cmake --build "%REPO%\build" || exit /b 1
ctest --test-dir "%REPO%\build" --output-on-failure || exit /b 1

if defined PACKAGE (
    cmake --build "%REPO%\build" --target package_plugin_only || exit /b 1
    cmake --build "%REPO%\build" --target package_ultimate_asi_loader || exit /b 1
)
