@echo off
REM Format C++ files in multiple directories using clang-format

REM Get the directory of this script
set "script_dir=%~dp0"
set "script_dir=%script_dir:~0,-1%"

REM Define directories to format
set "dirs=sources"

REM Loop through each target directory recursively
for %%D in (%dirs%) do (
    echo Running clang-format in: %script_dir%\..\%%D

    for /R "%script_dir%\..\%%D" %%F in (*.cpp *.h) do (
        echo Formatting %%F
        clang-format -i "%%~fF"
    )
)

echo Clang-format completed.
