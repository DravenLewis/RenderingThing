@echo off
setlocal

pushd "%~dp0" >nul
mingw32-make %*
set "exit_code=%ERRORLEVEL%"
popd >nul

exit /b %exit_code%
