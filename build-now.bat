@echo off
setlocal EnableDelayedExpansion
REM Compilar con VS 2019 en x64
set VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Community
set VCVARS=!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat

if not exist "!VCVARS!" (
  echo ERROR: No se encuentra VS2019 en !VS_PATH!
  exit /b 1
)

echo Inicializando entorno VS2019 x64...
call "!VCVARS!" x64
if errorlevel 1 (
  echo ERROR: Fallo vcvarsall.bat
  exit /b 1
)

echo Compilando WinUAE-DBG Test x64...
MSBuild.exe od-win32\winuae_msvc15\winuae_msvc.vcxproj /p:Configuration=Test /p:Platform=x64 /m /v:normal
