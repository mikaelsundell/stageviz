@echo off
REM SPDX-License-Identifier: BSD-3-Clause
REM Copyright (c) 2026 - present Mikael Sundell
setlocal DisableDelayedExpansion
where py >nul 2>&1
if errorlevel 1 goto python
py -3 "%~dp0CTest\run_all.py" %*
exit /b %errorlevel%
:python
where python >nul 2>&1
if errorlevel 1 goto missing
python "%~dp0CTest\run_all.py" %*
exit /b %errorlevel%
:missing
echo Python 3 is required to run Stageviz tests. 1>&2
exit /b 1
