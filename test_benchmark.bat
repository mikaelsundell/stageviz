@echo off
REM SPDX-License-Identifier: BSD-3-Clause
setlocal DisableDelayedExpansion
where py >nul 2>&1
if errorlevel 1 goto python
py -3 "%~dp0CTest\benchmark\run_stageviz_benchmark.py" %*
exit /b %errorlevel%
:python
python "%~dp0CTest\benchmark\run_stageviz_benchmark.py" %*
exit /b %errorlevel%
