@echo off
REM Copyright 2022-present Contributors to the stageviz project.
REM SPDX-License-Identifier: BSD-3-Clause
REM https://github.com/mikaelsundell/stageviz

set "app_dir=%~dp0"
set "app_name=Stageviz"
set "pkg_name=stageviz"
set "build_type="
set "deploy=0"
set "cmake_generator=Visual Studio 16 2019"

REM Parse arguments
:parse_args
if "%~1"=="" goto done_parse_args
set "arg=%~1"
if "%arg:~0,17%"=="--cmake_generator=" (
    set "cmake_generator=%arg:~17%"
) else if "%arg%"=="--deploy" (
    set "deploy=1"
) else (
    set "build_type=%arg%"
)
shift
goto parse_args
:done_parse_args

if "%build_type%"=="" (
    echo Invalid build type: Please specify 'Debug', 'Release', or 'all'
    exit /b 1
)

setlocal enabledelayedexpansion
set "ERRORLEVEL=0"
cls

REM validate build type
if not "%build_type%"=="Debug" (
    if not "%build_type%"=="Release" (
        if not "%build_type%"=="All" (
            echo Invalid build type: %build_type% (use 'Debug', 'Release', or 'All')
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

REM check if CMake version is compatible
for /f "tokens=*" %%V in ('cmake --version') do (
    for /f "tokens=*" %%V in ("%%V") do (
        set "cmake_version=%%V"
        set "cmake_version=!cmake_version:cmake version =!"
        for /f "delims=. tokens=1-3" %%a in ("!cmake_version!") do (
            set "major=%%a"
            set "minor=%%b"
            set "patch=%%c"
        )
    )
)

REM build stageviz
:build_stageviz

echo Building stageviz for %build_type% using %cmake_generator%
echo -------------------------------------------------------

set "build_dir=%app_dir%build.%build_type%"

echo Building in %build_dir%
mkdir "%build_dir%"
cd "%build_dir%"

REM prefix directory
if not defined THIRDPARTY_DIR (
    echo Could not find 3rdparty project environment variable THIRDPARTY_DIR
    goto :error
)

REM cmake friendly paths
set "cmake_dir=%app_dir:\=/%"
set "cmake_thirdparty_dir=%THIRDPARTY_DIR:\=/%"

REM generate build with cmake
cmake .. -G "%cmake_generator%" -DCMAKE_MODULE_PATH="%cmake_dir%modules" -DCMAKE_PREFIX_PATH="%cmake_thirdparty_dir%"
if errorlevel 1 goto :error

REM build the configuration
cmake --build . --config %build_type% --parallel --verbose
if errorlevel 1 goto :error

REM deploy the configuration if requested
if "%deploy%"=="1" (
    call :deploy_stageviz
)

goto :end

:deploy_stageviz
REM deploy using windeployqt
set "deploy_dir=%app_dir%deploy.%build_type%"
set "windeployqt=%THIRDPARTY_DIR%\bin\windeployqt6.exe"
set "exe_path=%build_dir%\%build_type%\%app_name%.exe"
set "resources_path=%build_dir%\%build_type%\resources"

echo Deploying stageviz for %build_type% to %deploy_dir%
echo -------------------------------------------------

REM check if windeployqt exists
if not exist "%windeployqt%" (
    echo windeployqt not found in %THIRDPARTY_DIR%/bin
    goto :error
)

REM clean deploy directory
if exist "%deploy_dir%" rmdir /s /q "%deploy_dir%"
mkdir "%deploy_dir%"

REM copy executable
copy "%exe_path%" "%deploy_dir%"

REM copy resources
xcopy "%resources_path%" "%deploy_dir%\resources" /E /I /Y

REM copy dependencies
copy "%THIRDPARTY_DIR%\bin\lcms2.dll" "%deploy_dir%"

REM run windeployqt
"%windeployqt%" "%deploy_dir%\%app_name%.exe" --dir "%deploy_dir%"
if errorlevel 1 goto :error

REM copy USD-related DLLs
set "dependencies=python39 tbb12 usd_ar usd_arch usd_boost usd_python usd_cameraUtil usd_js usd_garch usd_gf usd_geomUtil usd_glf usd_hd usd_hio usd_hdar usd_hdgp usd_hdx usd_hdsi usd_hdSt usd_hf usd_hgi usd_hgiInterop usd_hgiGL usd_hio usd_kind usd_ndr usd_pcp usd_plug usd_pxOsd usd_sdf usd_sdr usd_tf usd_ts usd_trace usd_usd usd_usdGeom usd_usdImaging usd_usdImagingGL usd_usdLux usd_usdRender usd_usdShade usd_usdVol usd_vt usd_work"

set "search_paths=%THIRDPARTY_DIR%\lib %THIRDPARTY_DIR%\bin %LOCALAPPDATA%\Programs\Python\Python39"

for %%D in (%dependencies%) do (
    set "found="
    for %%P in (%search_paths%) do (
        if exist "%%P\%%D.dll" (
            echo Copying %%D.dll from %%P
            copy "%%P\%%D.dll" "%deploy_dir%" >nul
            set "found=1"
        )
    )
    if not defined found (
        echo ERROR: Missing required dependency: %%D.dll
        goto :error
    )
)

REM copy plugin
set "plugin_path=%THIRDPARTY_DIR%\plugin"
set "plugin_deploy_path=%deploy_dir%\plugin"

if exist "%plugin_path%" (
    echo Copying usd plugins to %plugin_deploy_path%
    xcopy "%plugin_path%" "%plugin_deploy_path%" /E /I /Y
) else (
    echo Could not find usd plugin at %plugin_path%, skipping
)

REM copy usd
set "usd_path=%THIRDPARTY_DIR%\lib\usd"
set "usd_deploy_path=%deploy_dir%\usd"

if exist "%usd_path%" (
    echo Copying usd pluginInfo to %usd_deploy_path%
    xcopy "%usd_path%" "%usd_deploy_path%" /E /I /Y
) else (
    echo Could not find usd at %pluginusd_paths_usd_path%, skipping
)

echo deployment successful

REM create a zip file of the deployment folder

REM extract version from CMakeLists.txt
set "version_file=%app_dir%CMakeLists.txt"
set "version="

for /f "usebackq tokens=3 delims=() " %%A in (`findstr /c:"set (project_long_version" "%version_file%"`) do (
    set "version=%%A"
    set "version=!version:~1,-1!" 
)

if "%version%"=="" (
    echo failed to extract version from CMakeLists.txt
    goto :error
)

for /f "tokens=2 delims==" %%I in ('wmic os get localdatetime /value ^| find "LocalDateTime"') do set datetime=%%I
set "current_date=%datetime:~2,6%"

set "zipfile=%deploy_dir%\%app_name%_%version%_%current_date%_%build_type%.zip"
echo creating zip file: %zipfile%
powershell -command "Compress-Archive -Path '%deploy_dir%\*' -DestinationPath '%zipfile%' -Force"

if errorlevel 1 (
    echo failed to create ZIP file
    goto :error
)

echo zip file created successfully: %zipfile%

goto :end

:error
exit /b 1

:end
exit /b 0
