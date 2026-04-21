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

REM generate build with cmake
cmake .. -G "%cmake_generator%" -DCMAKE_MODULE_PATH="%cmake_dir%modules" -DCMAKE_PREFIX_PATH="%cmake_thirdparty_dir%" -DDEPLOY_BUILD=%deploy_build%
if errorlevel 1 (
    popd
    goto :error
)

REM build the configuration
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

REM THIRDPARTY_DIR is already the active config prefix.
set "prefix=%THIRDPARTY_DIR%"
set "prefix_bin=%prefix%\bin"
set "prefix_lib=%prefix%\lib"
set "prefix_plugin_usd=%prefix%\plugin\usd"
set "prefix_usd_root=%prefix_lib%\usd"

echo Deploying stageviz for %current_build_type% to %deploy_dir%
echo -------------------------------------------------
echo Using 3rdparty prefix: %prefix%

REM check if windeployqt exists
if not exist "%windeployqt%" (
    echo windeployqt not found in %prefix_bin%
    goto :error
)

if not exist "%prefix%" (
    echo 3rdparty config prefix not found: %prefix%
    goto :error
)

REM clean deploy directory
if exist "%deploy_dir%" rmdir /s /q "%deploy_dir%"
mkdir "%deploy_dir%"

REM copy executable
if not exist "%exe_path%" (
    echo Executable not found: %exe_path%
    goto :error
)
copy "%exe_path%" "%deploy_dir%" >nul
if errorlevel 1 goto :error

REM copy resources
if exist "%resources_path%" (
    xcopy "%resources_path%" "%deploy_dir%\resources" /E /I /Y >nul
) else (
    echo Resources not found at %resources_path%, skipping
)

REM copy extra dependencies
for %%D in (lcms2 zlib libpng16) do (
    if exist "%prefix_bin%\%%D.dll" (
        echo Copying %%D.dll from %prefix_bin%
        copy "%prefix_bin%\%%D.dll" "%deploy_dir%" >nul
        if errorlevel 1 goto :error
    ) else (
        echo WARNING: %%D.dll not found in %prefix_bin%
    )
)

REM run windeployqt
"%windeployqt%" "%deploy_dir%\%app_name%.exe" --dir "%deploy_dir%"
if errorlevel 1 goto :error

REM copy Python runtime
if exist "%LOCALAPPDATA%\Programs\Python\Python39\python39.dll" (
    echo Copying python39.dll from %LOCALAPPDATA%\Programs\Python\Python39
    copy "%LOCALAPPDATA%\Programs\Python\Python39\python39.dll" "%deploy_dir%" >nul
    if errorlevel 1 goto :error
) else (
    echo WARNING: python39.dll not found in %LOCALAPPDATA%\Programs\Python\Python39
)

REM copy TBB runtime
if exist "%prefix_bin%\tbb12.dll" (
    echo Copying tbb12.dll from %prefix_bin%
    copy "%prefix_bin%\tbb12.dll" "%deploy_dir%" >nul
    if errorlevel 1 goto :error
) else (
    echo WARNING: tbb12.dll not found in %prefix_bin%
)

REM copy USD-related DLLs from the actual config lib dir
set "usd_dependencies=usd_ar usd_arch usd_boost usd_python usd_cameraUtil usd_js usd_garch usd_gf usd_geomUtil usd_glf usd_hd usd_hdMtlx usd_hio usd_hdar usd_hdgp usd_hdx usd_hdsi usd_hdSt usd_hf usd_hgi usd_hgiInterop usd_hgiGL usd_kind usd_pcp usd_plug usd_pxOsd usd_sdf usd_sdr usd_tf usd_ts usd_trace usd_usd usd_usdGeom usd_usdImaging usd_usdImagingGL usd_usdLux usd_usdMtlx usd_usdRender usd_usdShade usdSkelImaging usd_usdUI usd_usdUtils usd_usdVol usd_vt usd_work"

for %%D in (%usd_dependencies%) do (
    if exist "%prefix_lib%\%%D.dll" (
        echo Copying %%D.dll from %prefix_lib%
        copy "%prefix_lib%\%%D.dll" "%deploy_dir%" >nul
        if errorlevel 1 goto :error
    ) else (
        echo WARNING: Missing USD runtime DLL: %prefix_lib%\%%D.dll
    )
)

REM copy usd plugin binaries
if exist "%prefix_plugin_usd%" (
    echo Copying usd plugins to %deploy_dir%\plugins\usd
    if exist "%deploy_dir%\plugins\usd" rmdir /s /q "%deploy_dir%\plugins\usd"
    mkdir "%deploy_dir%\plugins" 2>nul
    xcopy "%prefix_plugin_usd%" "%deploy_dir%\plugins\usd" /E /I /Y >nul
    if errorlevel 1 goto :error
) else (
    echo WARNING: Could not find usd plugin folder at %prefix_plugin_usd%
)

REM copy usd plugin metadata root
if exist "%prefix_usd_root%" (
    echo Copying usd plugin metadata root to %deploy_dir%\usd
    if exist "%deploy_dir%\usd" rmdir /s /q "%deploy_dir%\usd"
    xcopy "%prefix_usd_root%" "%deploy_dir%\usd" /E /I /Y >nul
    if errorlevel 1 goto :error
) else (
    echo WARNING: Could not find usd metadata root at %prefix_usd_root%
)

REM deploy python bindings
set "site_packages_dst=%deploy_dir%\site-packages"
if exist "%site_packages_dst%" rmdir /s /q "%site_packages_dst%"
mkdir "%site_packages_dst%"

set "python_projects=pxr PySide6 shiboken6 MaterialX OpenImageIO"

set "python_root_1=%prefix%\site-packages"
set "python_root_2=%prefix_lib%\site-packages"
set "python_root_3=%prefix%\Lib\site-packages"
set "python_root_4=%prefix_lib%\python"
set "python_root_5=%prefix%\python\Lib\site-packages"
set "python_root_6=%prefix%\python\lib\site-packages"
set "python_root_7=%prefix_lib%\python3.9\site-packages"
set "python_root_8=%prefix%\python\lib\python3.9\site-packages"

for %%P in (%python_projects%) do (
    set "package_src="

    if exist "!python_root_1!\%%P" set "package_src=!python_root_1!\%%P"
    if not defined package_src if exist "!python_root_2!\%%P" set "package_src=!python_root_2!\%%P"
    if not defined package_src if exist "!python_root_3!\%%P" set "package_src=!python_root_3!\%%P"
    if not defined package_src if exist "!python_root_4!\%%P" set "package_src=!python_root_4!\%%P"
    if not defined package_src if exist "!python_root_5!\%%P" set "package_src=!python_root_5!\%%P"
    if not defined package_src if exist "!python_root_6!\%%P" set "package_src=!python_root_6!\%%P"
    if not defined package_src if exist "!python_root_7!\%%P" set "package_src=!python_root_7!\%%P"
    if not defined package_src if exist "!python_root_8!\%%P" set "package_src=!python_root_8!\%%P"

    if defined package_src (
        echo Copying python package %%P
        echo   from: !package_src!
        echo   to:   %site_packages_dst%\%%P
        if exist "%site_packages_dst%\%%P" rmdir /s /q "%site_packages_dst%\%%P"
        xcopy "!package_src!" "%site_packages_dst%\%%P" /E /I /Y >nul
        if errorlevel 1 goto :error
    ) else (
        echo WARNING: Python package %%P not found in known search roots under %prefix%
    )
)

echo.
echo Deployed USD DLLs:
dir /b "%deploy_dir%\usd_*.dll" 2>nul

echo.
echo Deployed python packages:
dir /b "%site_packages_dst%" 2>nul

echo.
echo Deployment successful

REM extract version from CMakeLists.txt
set "version_file=%app_dir%CMakeLists.txt"
set "version="

for /f "usebackq tokens=2 delims= " %%A in (`findstr /c:"set(project_long_version" "%version_file%"`) do (
    set "version=%%A"
    set "version=!version:~1,-1!"
)

if "%version%"=="" (
    for /f "usebackq tokens=2 delims= " %%A in (`findstr /c:"set (project_long_version" "%version_file%"`) do (
        set "version=%%A"
        set "version=!version:~1,-1!"
    )
)

if "%version%"=="" (
    echo Failed to extract version from CMakeLists.txt
    goto :error
)

for /f "tokens=2 delims==" %%I in ('wmic os get localdatetime /value ^| find "LocalDateTime"') do set datetime=%%I
set "current_date=%datetime:~2,6%"

set "zipfile=%deploy_dir%\%app_name%_%version%_%current_date%_%current_build_type%.zip"
echo Creating zip file: %zipfile%
powershell -command "Compress-Archive -Path '%deploy_dir%\*' -DestinationPath '%zipfile%' -Force"
if errorlevel 1 (
    echo Failed to create ZIP file
    goto :error
)

echo ZIP file created successfully: %zipfile%
exit /b 0

:error
cd /d "%original_dir%"
exit /b 1

:end
cd /d "%original_dir%"
exit /b 0