@echo off
REM Copyright 2022-present Contributors to the stageviz project.
REM SPDX-License-Identifier: BSD-3-Clause
REM https://github.com/mikaelsundell/stageviz

setlocal
set "app_dir=%~dp0"
set "original_dir=%cd%"
set "app_name=Stageviz"
set "pkg_name=stageviz"
set "build_type="
set "deploy=0"
set "skip_build=0"
set "cmake_generator="
set "cmake_exe=cmake.exe"
set "cmake_instance="

REM Parse arguments
:parse_args
if "%~1"=="" goto done_parse_args
set "arg=%~1"
if "%arg:~0,18%"=="--cmake_generator=" (
    set "cmake_generator=%arg:~18%"
) else if /I "%arg%"=="--deploy" (
    set "deploy=1"
) else if /I "%arg%"=="--no-build" (
    set "skip_build=1"
) else (
    set "build_type=%arg%"
)
shift
goto parse_args
:done_parse_args

if "%build_type%"=="" (
    echo Invalid build type: Please specify 'Debug', 'Release', or 'All'
    cd /d "%original_dir%"
    exit /b 1
)

setlocal enabledelayedexpansion
cls

REM validate build type
if /I not "%build_type%"=="Debug" (
    if /I not "%build_type%"=="Release" (
        if /I not "%build_type%"=="All" (
            echo Invalid build type: %build_type% ^(use 'Debug', 'Release', or 'All'^)
            goto :error
        )
    )
)

if "%skip_build%"=="1" if "%deploy%"=="0" (
    echo --no-build requires --deploy. 1>&2
    goto :error
)

if "%deploy%"=="1" (
    call :check_deploy_python
    if errorlevel 1 goto :error
)

if "%skip_build%"=="0" (
    if defined cmake_generator (
        where cmake.exe >nul 2>&1
        if errorlevel 1 (
            echo CMake not found in PATH. 1>&2
            goto :error
        )
    ) else (
        call :find_visual_studio
        if errorlevel 1 goto :error
    )
)

if /I "%build_type%"=="All" (
    call :build_stageviz Debug
    if errorlevel 1 goto :error
    call :build_stageviz Release
    if errorlevel 1 goto :error
    goto :end
) else (
    call :build_stageviz %build_type%
    if errorlevel 1 goto :error
    goto :end
)

:check_deploy_python
set "python_cmd=py -3"
%python_cmd% -c "import sys" >nul 2>&1
if errorlevel 1 (
    set "python_cmd=python"
    python -c "import sys; sys.exit(sys.version_info.major != 3)" >nul 2>&1
    if errorlevel 1 (
        echo Windows deployment requires Python 3. Install it with the Python launcher or put python.exe on PATH. 1>&2
        exit /b 1
    )
)

%python_cmd% -c "import pefile" >nul 2>&1
if errorlevel 1 (
    echo Windows deployment requires pefile. Install it for the selected Python with 1>&2
    echo     %python_cmd% -m pip install pefile 1>&2
    exit /b 1
)
exit /b 0

:find_visual_studio
REM Match 3rdparty's Visual Studio and Build Tools discovery.
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" goto :missing_visual_studio
set "vs_install="
set "vs_version="
for /f "usebackq tokens=1,*" %%I in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format text`) do (
    if "%%I"=="installationPath:" set "vs_install=%%J"
    if "%%I"=="installationVersion:" set "vs_version=%%J"
)
if not defined vs_install goto :missing_visual_studio
if not defined vs_version goto :missing_visual_studio
for /f "delims=." %%V in ("%vs_version%") do set "vs_major=%%V"

REM Use the bundled CMake if the one on PATH lacks this generator.
where cmake.exe >nul 2>&1
if not errorlevel 1 call :find_cmake_generator
if not defined cmake_generator (
    set "cmake_exe=%vs_install%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if exist "!cmake_exe!" call :find_cmake_generator
)
if not defined cmake_generator (
    echo No CMake found that supports Visual Studio %vs_version%. Update CMake on PATH or install Visual Studio's C++ CMake tools. 1>&2
    exit /b 1
)
set cmake_instance="-DCMAKE_GENERATOR_INSTANCE=%vs_install%"
exit /b 0

:find_cmake_generator
for /f "tokens=1-4 delims=* " %%G in ('"%cmake_exe%" --help') do (
    if "%%G %%H %%I"=="Visual Studio %vs_major%" set "cmake_generator=%%G %%H %%I %%J"
)
exit /b 0

:missing_visual_studio
echo Visual Studio or Build Tools with the MSVC x64 C++ tools is required. 1>&2
exit /b 1

:build_stageviz
set "current_build_type=%~1"
set "build_dir=%app_dir%build.%current_build_type%"
if "%skip_build%"=="1" goto :deploy_existing_stageviz

echo Building stageviz for %current_build_type% using %cmake_generator%
echo -------------------------------------------------------

if exist "%build_dir%" rmdir /s /q "%build_dir%"
mkdir "%build_dir%"

pushd "%build_dir%"
if errorlevel 1 (
    echo Failed to enter build directory: %build_dir%
    goto :error
)

REM prefix directory
if not defined THIRDPARTY_DIR (
    echo Could not find 3rdparty project environment variable THIRDPARTY_DIR
    popd
    goto :error
)

REM cmake friendly paths
set "cmake_dir=%app_dir:\=/%"
set "cmake_thirdparty_dir=%THIRDPARTY_DIR:\=/%"

REM DEPLOY_BUILD must only be ON for packaged/deploy builds
set "deploy_build=OFF"
if "%deploy%"=="1" (
    set "deploy_build=ON"
    echo Deployment enabled
)

"%cmake_exe%" .. -G "%cmake_generator%" %cmake_instance% -DCMAKE_MODULE_PATH="%cmake_dir%modules" -DCMAKE_PREFIX_PATH="%cmake_thirdparty_dir%" -DDEPLOY_BUILD=%deploy_build%
if errorlevel 1 (
    popd
    goto :error
)

"%cmake_exe%" --build . --config %current_build_type% --parallel --verbose
if errorlevel 1 (
    popd
    goto :error
)

REM deploy the configuration if requested
if "%deploy%"=="1" (
    call :deploy_stageviz %current_build_type%
    if errorlevel 1 (
        popd
        goto :error
    )
)

popd
exit /b 0

:deploy_existing_stageviz
if not defined THIRDPARTY_DIR (
    echo Could not find 3rdparty project environment variable THIRDPARTY_DIR 1>&2
    exit /b 1
)

findstr /L /X /C:"DEPLOY_BUILD:BOOL=ON" "%build_dir%\CMakeCache.txt" >nul 2>&1
if errorlevel 1 (
    echo No deployment build is configured in "%build_dir%". Build %current_build_type% with --deploy first. 1>&2
    exit /b 1
)

echo Packaging existing %current_build_type% build
call :deploy_stageviz %current_build_type%
exit /b %errorlevel%

:deploy_stageviz
set "current_build_type=%~1"
set "deploy_dir=%app_dir%deploy.%current_build_type%"
set "exe_path=%build_dir%\%current_build_type%\%app_name%.exe"
set "resources_path=%build_dir%\%current_build_type%\resources"
set "windeployqt=%THIRDPARTY_DIR%\bin\windeployqt6.exe"
set "deploy_script=%app_dir%scripts\deploywin.py"

REM THIRDPARTY_DIR is already the active config prefix.
set "prefix=%THIRDPARTY_DIR%"
set "prefix_bin=%prefix%\bin"
set "prefix_lib=%prefix%\lib"
set "prefix_plugin_root=%prefix%\plugin"
set "prefix_plugin_usd=%prefix_plugin_root%\usd"
set "prefix_usd_root=%prefix_lib%\usd"

REM locate the active python runtime
for /f "delims=" %%P in ('%python_cmd% -c "import os, sys; print(os.path.dirname(sys.executable))"') do set "python_runtime_dir=%%P"

REM search paths for deploywin.py
set "deploy_search_paths=%prefix%;%prefix_bin%;%prefix_lib%;%prefix%\python;%prefix_lib%\python;%prefix_plugin_root%;%prefix_plugin_usd%;%prefix_usd_root%;%python_runtime_dir%;%python_runtime_dir%\DLLs"

REM directories that deploywin.py should copy as-is
set "copy_dirs=%prefix_plugin_usd%|plugin\usd|plugins;%prefix_usd_root%|usd;%python_runtime_dir%\DLLs|DLLs;%python_runtime_dir%\Lib|Lib"

REM python package discovery roots
set "python_roots=%prefix%\site-packages;%prefix_lib%\site-packages;%prefix%\Lib\site-packages;%prefix%\python;%prefix_lib%\python;%prefix%\python\Lib\site-packages;%prefix%\python\lib\site-packages;%prefix_lib%\python3.9\site-packages;%prefix%\python\lib\python3.9\site-packages"

REM python packages to deploy into site-packages
set "python_packages=pxr;PySide6;shiboken6;MaterialX;OpenImageIO;PyOpenColorIO"

echo Deploying stageviz for %current_build_type% to %deploy_dir%
echo -------------------------------------------------
echo Using 3rdparty prefix: %prefix%

if not exist "%windeployqt%" (
    echo ERROR: windeployqt not found: %windeployqt%
    goto :error
)

if not exist "%deploy_script%" (
    echo ERROR: deploy script not found: %deploy_script%
    goto :error
)

if not exist "%prefix%" (
    echo ERROR: 3rdparty config prefix not found: %prefix%
    goto :error
)

if not exist "%exe_path%" (
    echo ERROR: Executable not found: %exe_path%
    goto :error
)

REM clean deploy directory
if exist "%deploy_dir%" rmdir /s /q "%deploy_dir%"
mkdir "%deploy_dir%"
if errorlevel 1 goto :error

REM copy resources before runtime deployment
if exist "%resources_path%" (
    echo Copying resources...
    xcopy "%resources_path%" "%deploy_dir%\resources" /E /I /Y >nul
    if errorlevel 1 goto :error
) else (
    echo Resources not found at %resources_path%, skipping
)

REM run windeployqt first so Qt runtime/plugins are in place
echo Running windeployqt...
"%windeployqt%" "%exe_path%" --dir "%deploy_dir%"
if errorlevel 1 goto :error

REM run recursive deployment script for DLLs, plugin folders and python packages
echo Running deploywin.py...
%python_cmd% "%deploy_script%" ^
    "%exe_path%" ^
    "%deploy_dir%" ^
    "%deploy_search_paths%" ^
    "%copy_dirs%" ^
    "%python_roots%" ^
    "%python_packages%"
if errorlevel 1 goto :error

REM extract version from CMakeLists.txt
set "version_file=%app_dir%CMakeLists.txt"
set "version="
for /f "usebackq tokens=2 delims= " %%A in (`findstr /c:"set(project_long_version" "%version_file%"`) do (
    set "version=%%A"
)

if "!version!"=="" (
    for /f "usebackq tokens=2 delims= " %%A in (`findstr /c:"set ^(project_long_version" "%version_file%"`) do (
        set "version=%%A"
    )
)
if "!version!"=="" (
    echo Failed to extract version from CMakeLists.txt
    goto :error
)

REM clean surrounding quotes and trailing parenthesis
set "version=!version:"=!"
set "version=!version:)=!"

REM generate deployment date as YYMMDD using PowerShell
set "current_date="
for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyMMdd"') do set "current_date=%%I"

if not defined current_date (
    echo Failed to determine current date
    goto :error
)

set "zipfile=%deploy_dir%\%app_name%_%version%_%current_date%_%current_build_type%.zip"
echo Creating zip file: %zipfile%

%python_cmd% "%deploy_script%" --archive "%deploy_dir%" "%zipfile%"
if errorlevel 1 (
    echo Failed to create ZIP file
    goto :error
)

echo ZIP file created successfully: %zipfile%
echo.
echo Deployment successful
exit /b 0

:error
cd /d "%original_dir%"
exit /b 1

:end
cd /d "%original_dir%"
exit /b 0
