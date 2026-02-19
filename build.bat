@echo off
setlocal
REM build.bat - Compila WinUAE-DBG desde linea de comandos
REM Requiere: NASM en PATH o en C:\Program Files\NASM
REM Usa Visual Studio 18 (2026) por defecto. Override: set VS_PATH=C:\...\Community

set "ROOT=%~dp0"
cd /d "%ROOT%"

REM NASM
if exist "C:\Program Files\NASM" set "PATH=C:\Program Files\NASM;%PATH%"

REM Visual Studio: usar VS_PATH si esta definido, sino VS 18
if not defined VS_PATH set "VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Community"
if not exist "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" (
  echo Error: No se encuentra vcvarsall.bat en %VS_PATH%
  echo Define VS_PATH apuntando a tu instalacion de VS Community/BuildTools.
  exit /b 1
)

REM Plataforma: x64 por defecto (recomendado), Win32 si se pasa como argumento
set "PLAT=x64"
set "VCVARS_ARCH=x64"
if /i "%1"=="win32" (
  set "PLAT=Win32"
  set "VCVARS_ARCH=x86"
)

echo Compilando WinUAE-DBG %PLAT% Release...
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" %VCVARS_ARCH%
if errorlevel 1 (
  echo Error al inicializar el entorno de compilacion.
  exit /b 1
)

set "VCTargetsPath=%VS_PATH%\MSBuild\Microsoft\VC\v180\"
"%VS_PATH%\MSBuild\Current\Bin\MSBuild.exe" ^
  "od-win32\winuae_msvc15\winuae_msvc.sln" ^
  /p:Configuration=Release /p:Platform=%PLAT% /t:Build /m /v:minimal

set "BUILD_EXIT=%errorlevel%"
if %BUILD_EXIT% equ 0 (
  echo.
  echo Compilacion OK. Ejecutable: bin\winuae-gdb.exe para x64, bin\winuae-gdb-x86.exe para Win32
  echo Copiado a amiga-debug extension si el target CopyToAmigaDebug se ejecuto.
) else (
  echo Compilacion fallida.
)
exit /b %BUILD_EXIT%
