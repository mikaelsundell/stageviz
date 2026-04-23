@echo off
REM Copyright 2022-present Contributors to the stageviz project.
REM SPDX-License-Identifier: BSD-3-Clause
REM https://github.com/mikaelsundell/stageviz

set "app_dir=%~dp0"
set "original_dir=%cd%"
set "app_name=Stageviz"
set "pkg_name=stageviz"
set "build_type="
set "deploy=0"
set "cmake_generator=Visual Studio 17 2022"

REM Parse arguments
:parse_args
if "%~1"=="" goto done_parse_args
set "arg=%~1"
if "%arg:~0,18%"=="--cmake_generator=" (
    set "cmake_generator=%arg:~18%"
) else if /I "%arg%"=="--deploy" (
    set "deploy=1"
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

REM check if CMake is in the PATH
where cmake >nul 2>&1
if errorlevel 1 (
    echo CMake not found in the PATH, please make sure it's installed
    goto :error
)

REM check if CMake version is available
for /f "tokens=*" %%V in ('cmake --version') do (
    for /f "tokens=*" %%W in ("%%V") do (
        set "cmake_version=%%W"
        set "cmake_version=!cmake_version:cmake version =!"
        for /f "delims=. tokens=1-3" %%a in ("!cmake_version!") do (
            set "major=%%a"
            set "minor=%%b"
            set "patch=%%c"
        )
        goto cmake_version_done
    )
)
:cmake_version_done

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

:build_stageviz
set "current_build_type=%~1"

echo Building stageviz for %current_build_type% using %cmake_generator%
echo -------------------------------------------------------

set "build_dir=%app_dir%build.%current_build_type%"

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
if "%deploy%"=="1" set "deploy_build=ON"

echo DEPLOY_BUILD=%deploy_build%

cmake .. -G "%cmake_generator%" -DCMAKE_MODULE_PATH="%cmake_dir%modules" -DCMAKE_PREFIX_PATH="%cmake_thirdparty_dir%" -DDEPLOY_BUILD=%deploy_build%
if errorlevel 1 (
    popd
    goto :error
)

cmake --build . --config %current_build_type% --parallel --verbose
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

REM Prefer launcher if available, otherwise fall back to python
set "python_cmd=py -3"
where py >nul 2>&1
if errorlevel 1 set "python_cmd=python"

REM Search paths for deploywin.py
set "deploy_search_paths=%prefix%;%prefix_bin%;%prefix_lib%;%prefix%\python;%prefix_lib%\python;%prefix_plugin_root%;%prefix_plugin_usd%;%prefix_usd_root%"

REM Directories that deploywin.py should copy as-is
set "copy_dirs=%prefix_plugin_usd%|plugin\usd;%prefix_usd_root%|usd"

REM Python package discovery roots
set "python_roots=%prefix%\site-packages;%prefix_lib%\site-packages;%prefix%\Lib\site-packages;%prefix%\python;%prefix_lib%\python;%prefix%\python\Lib\site-packages;%prefix%\python\lib\site-packages;%prefix_lib%\python3.9\site-packages;%prefix%\python\lib\python3.9\site-packages"

REM Python packages to deploy into site-packages
set "python_packages=pxr;PySide6;shiboken6;MaterialX;OpenImageIO"

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

echo.
echo Deployment successful

:error
cd /d "%original_dir%"
exit /b 1

:end
cd /d "%original_dir%"
exit /b 0