@echo off
REM SPDX-License-Identifier: BSD-3-Clause
setlocal DisableDelayedExpansion
where py >nul 2>&1
if errorlevel 1 goto python
py -3 "%~dp0clang_format.py" %*
exit /b %errorlevel%
:python
where python >nul 2>&1
if errorlevel 1 goto missing
python "%~dp0clang_format.py" %*
exit /b %errorlevel%
:missing
echo Python 3 is required to select changed files for clang-format. 1>&2
exit /b 1
