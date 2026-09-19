@echo off
rem Windows 용 진입점 : NCS 툴체인의 파이썬으로 fw.py 를 실행한다.
setlocal
if "%NCS_ROOT%"=="" set "NCS_ROOT=C:\ncs"

set "PY="
for /d %%D in ("%NCS_ROOT%\toolchains\*") do (
  if exist "%%D\opt\bin\python.exe" set "PY=%%D\opt\bin\python.exe"
)
if not defined PY (
  where python >nul 2>nul && set "PY=python"
)
if not defined PY (
  echo [fw] ERROR: python not found ^(NCS_ROOT=%NCS_ROOT%^) 1>&2
  exit /b 1
)

"%PY%" "%~dp0fw.py" %*
exit /b %ERRORLEVEL%
